# End-to-end test of an HTTP binary cache served by nginx.
#
# A "substituter" VM builds and uploads paths to a static binary cache that
# nginx serves with various Content-Encoding settings; an "importer" VM then
# exercises substitution through `builtins.fetchurl`/`fetchTarball`,
# `nix copy --from`, `nix log` and `nix-prefetch-url`, including the negative
# case that `fetchTree` does not substitute non-final inputs.

{ lib, config, ... }:

let
  pkgs = config.nodes.substituter.nixpkgs.pkgs;

  ztdCompressedFile = pkgs.stdenv.mkDerivation {
    name = "dummy-zstd-compressed-archive";
    dontUnpack = true;
    nativeBuildInputs = with pkgs; [ zstd ];
    buildPhase = ''
      mkdir archive
      for _ in {1..100}; do echo "lorem" > archive/file1; done
      for _ in {1..100}; do echo "ipsum" > archive/file2; done
      tar --zstd -cf archive.tar.zst archive
    '';
    installPhase = ''
      install -Dm 644 -T archive.tar.zst $out/share/archive
    '';
  };

  # Bare derivation so we have a build log to upload.
  testDrv = builtins.toFile "test.nix" ''
    derivation {
      name = "test-package";
      builder = "/bin/sh";
      args = [ "-c" "echo 'Building test package...' >&2; echo 'hello from test package' > $out; echo 'Build complete!' >&2" ];
      system = builtins.currentSystem;
    }
  '';
in
{
  name = "http-binary-cache";

  nodes.substituter =
    { pkgs, ... }:
    {
      virtualisation.writableStore = true;

      nix.settings.substituters = lib.mkForce [ ];
      nix.settings.extra-experimental-features = [
        "nix-command"
        "fetch-tree"
      ];

      networking.firewall.allowedTCPPorts = [ 80 ];

      services.nginx.enable = true;
      services.nginx.virtualHosts."substituter" = {
        root = "${ztdCompressedFile}/share/";
        # Compress everything on the fly so that nix-prefetch-url has to deal
        # with a Content-Encoding header that does not match the payload.
        extraConfig = ''
          gzip on;
          gzip_types *;
          gzip_proxied any;
          gzip_min_length 0;
        '';

        # WebDAV upload endpoint for `nix copy --to http://...`.
        locations."/cache-upload" = {
          root = "/var/lib/nginx-cache";
          extraConfig = ''
            client_body_temp_path /var/lib/nginx-cache/tmp;
            create_full_put_path on;
            dav_methods PUT DELETE;
            dav_access user:rw group:rw all:r;
            gzip off;
            rewrite ^/cache-upload/(.*)$ /cache/$1 break;
          '';
        };

        # Download endpoint, advertising the on-disk compression of
        # narinfo/ls/log files via Content-Encoding.
        locations."/cache" = {
          root = "/var/lib/nginx-cache";
          extraConfig = ''
            gzip off;

            location ~ \.narinfo$ {
              add_header Content-Encoding x-gzip;
              default_type "text/x-nix-narinfo";
            }

            location ~ \.ls$ {
              add_header Content-Encoding gzip;
              default_type "application/json";
            }

            location ~ ^/cache/log/ {
              add_header Content-Encoding br;
              default_type "text/plain";
            }
          '';
        };
      };

      systemd.services.nginx.serviceConfig = {
        StateDirectory = "nginx-cache";
        StateDirectoryMode = "0755";
      };

      environment.systemPackages = with pkgs; [
        file
        gzip
        brotli
      ];

      environment.etc."nix/secret-key".text = ''
        substituter:SerxxAca5NEsYY0DwVo+subokk+OoHcD9m6JwuctzHgSQVfGHe6nCc+NReDjV3QdFYPMGix4FMg0+K/TM1B3aA==
      '';
    };

  nodes.importer =
    { lib, ... }:
    {
      virtualisation.writableStore = true;

      nix.settings = {
        extra-experimental-features = [
          "nix-command"
          "fetch-tree"
        ];
        substituters = lib.mkForce [ "http://substituter/cache" ];
        trusted-public-keys = lib.mkForce [ "substituter:EkFXxh3upwnPjUXg41d0HRWDzBoseBTINPiv0zNQd2g=" ];
      };
    };

  testScript = # python
    ''
      import json
      import os

      start_all()

      substituter.wait_for_unit("nginx.service")
      substituter.wait_for_open_port(80)
      importer.wait_for_unit("multi-user.target")

      # narinfo/ls/log compression must match the Content-Encoding headers
      # nginx attaches in the /cache location.
      cache_upload = (
          "http://localhost/cache-upload?secret-key=/etc/nix/secret-key"
          "&narinfo-compression=gzip&ls-compression=gzip&log-compression=br"
      )
      cache_dir = "/var/lib/nginx-cache/cache"

      # nix-prefetch-url must not let the on-the-fly gzip Content-Encoding
      # corrupt an archive that is already compressed (here: zstd).
      curl_output = substituter.succeed("curl --compressed -v http://localhost/archive 2>&1")
      assert "content-encoding: gzip" in curl_output.lower(), curl_output

      archive_path = substituter.succeed(
          "nix-prefetch-url http://localhost/archive --print-path | tail -n1"
      ).strip()
      mime_type = substituter.succeed(f"file --brief --mime-type {archive_path}").strip()
      assert mime_type == "application/zstd", mime_type
      substituter.succeed(f"tar --zstd -xf {archive_path}")

      # Build a derivation and publish it (with NAR listing and log) to the
      # compressed binary cache.
      out_path = substituter.succeed(
          "nix build --store /var/lib/build-store -f ${testDrv} --print-out-paths --print-build-logs"
      ).strip()
      drv_path = substituter.succeed(
          f"nix path-info --store /var/lib/build-store --derivation {out_path}"
      ).strip()

      substituter.succeed(
          f"nix copy --store /var/lib/build-store"
          f" --to '{cache_upload}&write-nar-listing=1' {out_path}"
      )
      substituter.succeed(
          f"nix store copy-log --store /var/lib/build-store --to '{cache_upload}' {drv_path}"
      )

      narinfo_hash = os.path.basename(out_path).split("-")[0]
      drv_name = os.path.basename(drv_path)

      substituter.succeed(f"gzip -t {cache_dir}/{narinfo_hash}.narinfo")
      substituter.succeed(f"gzip -t {cache_dir}/{narinfo_hash}.ls")
      substituter.succeed(f"brotli -t {cache_dir}/log/{drv_name}")

      narinfo_headers = substituter.succeed(f"curl -I http://localhost/cache/{narinfo_hash}.narinfo 2>&1")
      assert "content-encoding: x-gzip" in narinfo_headers.lower(), narinfo_headers
      ls_headers = substituter.succeed(f"curl -I http://localhost/cache/{narinfo_hash}.ls 2>&1")
      assert "content-encoding: gzip" in ls_headers.lower(), ls_headers
      log_headers = substituter.succeed(f"curl -I http://localhost/cache/log/{drv_name} 2>&1")
      assert "content-encoding: br" in log_headers.lower(), log_headers

      # The importer can read narinfo, NAR listing, NAR and logs back over HTTP.
      narinfo = importer.succeed(f"curl --compressed -fsS http://substituter/cache/{narinfo_hash}.narinfo")
      assert f"StorePath: {out_path}" in narinfo, narinfo
      assert "Sig: substituter:" in narinfo, narinfo

      listing = json.loads(importer.succeed(f"curl --compressed -fsS http://substituter/cache/{narinfo_hash}.ls"))
      assert listing["root"]["type"] == "regular", listing

      importer.succeed(f"nix-store --realise {out_path}")
      assert importer.succeed(f"cat {out_path}").strip() == "hello from test package"

      log_output = importer.succeed(f"nix log --store http://substituter/cache {drv_path}")
      assert "Building test package" in log_output, log_output

      # `nix copy --from` works against the same cache.
      importer.succeed(f"nix copy --from http://substituter/cache --no-check-sigs {out_path}")

      # builtins.fetchurl is substituted from the binary cache.
      missing_file = "/only-on-substituter.txt"
      substituter.succeed(f"echo 'this should only exist on the substituter' > {missing_file}")
      file_hash = substituter.succeed(f"nix hash file {missing_file}").strip()
      file_store_path = json.loads(substituter.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchurl {{ url = "file://{missing_file}"; sha256 = "{file_hash}"; }}
        '
      """))
      substituter.succeed(f"nix copy --to '{cache_upload}' {file_store_path}")
      importer.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchurl {{ url = "file://{missing_file}"; sha256 = "{file_hash}"; }}
        '
      """)

      # builtins.fetchTarball is substituted from the binary cache.
      missing_tarball = "/only-on-substituter.tar.gz"
      substituter.succeed("""
        mkdir -p /tmp/test-tarball
        echo 'Hello from tarball!' > /tmp/test-tarball/hello.txt
        echo 'Another file' > /tmp/test-tarball/file2.txt
      """)
      substituter.succeed(f"tar czf {missing_tarball} -C /tmp test-tarball")

      # Fetch once without a hash to learn the store path, then derive the
      # hashes the importer needs.
      tarball_store_path = json.loads(substituter.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTarball {{ url = "file://{missing_tarball}"; }}
        '
      """))
      path_info = json.loads(substituter.succeed(
          f"nix path-info --json-format 2 --json {tarball_store_path}"
      ))["info"]
      tarball_hash_sri = path_info[os.path.basename(tarball_store_path)]["narHash"]
      tarball_hash = substituter.succeed(f"nix-store --query --hash {tarball_store_path}").strip()
      substituter.succeed(f"nix copy --to '{cache_upload}' {tarball_store_path}")

      result_path = json.loads(importer.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTarball {{ url = "file://{missing_tarball}"; sha256 = "{tarball_hash}"; }}
        '
      """))
      content = importer.succeed(f"cat {result_path}/hello.txt").strip()
      assert content == "Hello from tarball!", content

      # fetchTree does NOT substitute non-final inputs: without __final it
      # must perform the real fetch (to preserve metadata like lastModified),
      # so it fails since the file only exists on the substituter.
      output = importer.fail(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTree {{
            type = "tarball";
            url = "file://{missing_tarball}";
            narHash = "{tarball_hash_sri}";
          }}
        ' 2>&1
      """)
      assert "does not exist" in output or "Couldn't open file" in output, output
    '';
}
