# Test content encoding support in Nix:
# 1. Fetching compressed files from servers with Content-Encoding headers
#    (e.g., fetching a zstd archive from a server using gzip Content-Encoding
#    should preserve the zstd format, not double-decompress)
# 2. HTTP binary cache store upload/download with compression support

{ lib, config, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

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

  # Bare derivation for testing binary cache with logs
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
  name = "content-encoding";

  nodes = {
    machine =
      { pkgs, ... }:
      {
        networking.firewall.allowedTCPPorts = [ 80 ];

        services.nginx.enable = true;
        services.nginx.virtualHosts."localhost" = {
          root = "${ztdCompressedFile}/share/";
          # Make sure that nginx really tries to compress the
          # file on the fly with no regard to size/mime.
          # http://nginx.org/en/docs/http/ngx_http_gzip_module.html
          extraConfig = ''
            gzip on;
            gzip_types *;
            gzip_proxied any;
            gzip_min_length 0;
          '';

          # Upload endpoint with WebDAV
          locations."/cache-upload" = {
            root = "/var/lib/nginx-cache";
            extraConfig = ''
              client_body_temp_path /var/lib/nginx-cache/tmp;
              create_full_put_path on;
              dav_methods PUT DELETE;
              dav_access user:rw group:rw all:r;

              # Don't try to compress already compressed files
              gzip off;

              # Rewrite to remove -upload suffix when writing files
              rewrite ^/cache-upload/(.*)$ /cache/$1 break;
            '';
          };

          # Download endpoint with Content-Encoding headers
          locations."/cache" = {
            root = "/var/lib/nginx-cache";
            extraConfig = ''
              gzip off;

              # Serve .narinfo files with gzip encoding
              location ~ \.narinfo$ {
                add_header Content-Encoding gzip;
                default_type "text/x-nix-narinfo";
              }

              # Serve .ls files with gzip encoding
              location ~ \.ls$ {
                add_header Content-Encoding gzip;
                default_type "application/json";
              }

              # Serve log files with brotli encoding
              location ~ ^/cache/log/ {
                add_header Content-Encoding br;
                default_type "text/plain";
              }
            '';
          };
        };

        systemd.services.nginx = {
          serviceConfig = {
            StateDirectory = "nginx-cache";
            StateDirectoryMode = "0755";
          };
        };

        environment.systemPackages = with pkgs; [
          file
          gzip
          brotli
          curl
        ];

        virtualisation.writableStore = true;
        nix.settings.substituters = lib.mkForce [ ];
        nix.settings.experimental-features = [
          "nix-command"
          "flakes"
        ];
      };
  };

  # Check that when nix-prefetch-url is used with a zst tarball it does not get decompressed.
  # Also test HTTP binary cache store with compression support.
  testScript = ''
    # fmt: off
    start_all()

    machine.wait_for_unit("nginx.service")

    # Original test: zstd archive with gzip content-encoding
    # Make sure that the file is properly compressed as the test would be meaningless otherwise
    curl_output = machine.succeed("curl --compressed -v http://localhost/archive 2>&1")
    assert "content-encoding: gzip" in curl_output.lower(), f"Expected 'content-encoding: gzip' in curl output, but got: {curl_output}"

    archive_path = machine.succeed("nix-prefetch-url http://localhost/archive --print-path | tail -n1").strip()
    mime_type = machine.succeed(f"file --brief --mime-type {archive_path}").strip()
    assert mime_type == "application/zstd", f"Expected archive to be 'application/zstd', but got: {mime_type}"
    machine.succeed(f"tar --zstd -xf {archive_path}")

    # Test HTTP binary cache store with compression
    outPath = machine.succeed("""
      nix build --store /var/lib/build-store -f ${testDrv} --print-out-paths --print-build-logs
    """).strip()

    drvPath = machine.succeed(f"""
      nix path-info --store /var/lib/build-store --derivation {outPath}
    """).strip()

    # Upload to cache with compression (use cache-upload endpoint)
    machine.succeed(f"""
      nix copy --store /var/lib/build-store --to 'http://localhost/cache-upload?narinfo-compression=gzip&ls-compression=gzip&write-nar-listing=1' {outPath} -vvvvv 2>&1 | tail -100
    """)
    machine.succeed(f"""
      nix store copy-log --store /var/lib/build-store --to 'http://localhost/cache-upload?log-compression=br' {drvPath} -vvvvv 2>&1 | tail -100
    """)

    # List cache contents
    print(machine.succeed("find /var/lib/nginx-cache -type f"))

    narinfoHash = outPath.split('/')[3].split('-')[0]
    drvName = drvPath.split('/')[3]

    # Verify compression
    machine.succeed(f"gzip -t /var/lib/nginx-cache/cache/{narinfoHash}.narinfo")
    machine.succeed(f"gzip -t /var/lib/nginx-cache/cache/{narinfoHash}.ls")
    machine.succeed(f"brotli -t /var/lib/nginx-cache/cache/log/{drvName}")

    # Check Content-Encoding headers on the download endpoint
    narinfo_headers = machine.succeed(f"curl -I http://localhost/cache/{narinfoHash}.narinfo 2>&1")
    assert "content-encoding: gzip" in narinfo_headers.lower(), f"Expected 'content-encoding: gzip' for .narinfo file, but headers were: {narinfo_headers}"

    ls_headers = machine.succeed(f"curl -I http://localhost/cache/{narinfoHash}.ls 2>&1")
    assert "content-encoding: gzip" in ls_headers.lower(), f"Expected 'content-encoding: gzip' for .ls file, but headers were: {ls_headers}"

    log_headers = machine.succeed(f"curl -I http://localhost/cache/log/{drvName} 2>&1")
    assert "content-encoding: br" in log_headers.lower(), f"Expected 'content-encoding: br' for log file, but headers were: {log_headers}"

    # Test fetching from cache
    machine.succeed(f"nix copy --from 'http://localhost/cache' --no-check-sigs {outPath}")

    # Test log retrieval
    log_output = machine.succeed(f"nix log --store 'http://localhost/cache' {drvPath} 2>&1")
    assert "Building test package" in log_output, f"Expected 'Building test package' in log output, but got: {log_output}"
  '';
}
