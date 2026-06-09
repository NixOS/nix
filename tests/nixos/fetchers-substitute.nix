{
  name = "fetchers-substitute";

  nodes.substituter =
    { pkgs, ... }:
    {
      virtualisation.writableStore = true;

      nix.settings.extra-experimental-features = [
        "nix-command"
        "fetch-tree"
      ];

      networking.firewall.allowedTCPPorts = [ 80 ];

      systemd.tmpfiles.rules = [ "d /var/cache/binary-cache 0755 root root -" ];

      services.nginx = {
        enable = true;
        virtualHosts."substituter".root = "/var/cache/binary-cache";
      };

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
        substituters = lib.mkForce [ "http://substituter" ];
        trusted-public-keys = lib.mkForce [ "substituter:EkFXxh3upwnPjUXg41d0HRWDzBoseBTINPiv0zNQd2g=" ];
      };
    };

  testScript = # python
    ''
      import json
      import os

      start_all()

      substituter.wait_for_unit("multi-user.target")
      importer.wait_for_unit("multi-user.target")

      binary_cache = "file:///var/cache/binary-cache?secret-key=/etc/nix/secret-key"

      # builtins.fetchurl is substituted
      missing_file = "/only-on-substituter.txt"

      substituter.succeed(f"echo 'this should only exist on the substituter' > {missing_file}")

      file_hash = substituter.succeed(f"nix hash file {missing_file}").strip()

      file_store_path_json = substituter.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchurl {{
            url = "file://{missing_file}";
            sha256 = "{file_hash}";
          }}
        '
      """)

      file_store_path = json.loads(file_store_path_json)

      substituter.succeed(f"nix copy --to '{binary_cache}' {file_store_path}")

      importer.succeed(f"""
        nix-instantiate -vvvvv --eval --json --read-write-mode --expr '
          builtins.fetchurl {{
            url = "file://{missing_file}";
            sha256 = "{file_hash}";
          }}
        '
      """)

      # builtins.fetchTarball is substituted
      missing_tarball = "/only-on-substituter.tar.gz"

      substituter.succeed("""
        mkdir -p /tmp/test-tarball
        echo 'Hello from tarball!' > /tmp/test-tarball/hello.txt
        echo 'Another file' > /tmp/test-tarball/file2.txt
      """)
      substituter.succeed(f"tar czf {missing_tarball} -C /tmp test-tarball")

      # Fetch once without a hash to learn the store path, then derive the
      # hashes the importer needs.
      tarball_store_path_json = substituter.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTarball {{
            url = "file://{missing_tarball}";
          }}
        '
      """)

      tarball_store_path = json.loads(tarball_store_path_json)

      path_info_json = substituter.succeed(f"nix path-info --json-format 2 --json {tarball_store_path}").strip()
      path_info_dict = json.loads(path_info_json)["info"]
      tarball_hash_sri = path_info_dict[os.path.basename(tarball_store_path)]["narHash"]

      tarball_hash = substituter.succeed(f"nix-store --query --hash {tarball_store_path}").strip()

      substituter.succeed(f"nix copy --to '{binary_cache}' {tarball_store_path}")

      result = importer.succeed(f"""
        nix-instantiate -vvvvv --eval --json --read-write-mode --expr '
          builtins.fetchTarball {{
            url = "file://{missing_tarball}";
            sha256 = "{tarball_hash}";
          }}
        '
      """)

      result_path = json.loads(result)

      content = importer.succeed(f"cat {result_path}/hello.txt").strip()
      assert content == "Hello from tarball!", f"Content mismatch: {content}"

      # fetchTree does NOT substitute non-final inputs: without __final it
      # must perform the real fetch (to preserve metadata like lastModified),
      # so it fails since the file only exists on the substituter.
      output = importer.fail(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTree {{
            type = "tarball";
            url = "file:///only-on-substituter.tar.gz";
            narHash = "{tarball_hash_sri}";
          }}
        ' 2>&1
      """)

      assert "does not exist" in output or "Couldn't open file" in output, f"Expected download failure, got: {output}"
    '';
}
