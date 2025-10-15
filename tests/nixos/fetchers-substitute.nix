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

      networking.firewall.allowedTCPPorts = [ 5000 ];

      services.nix-serve = {
        enable = true;
        secretKeyFile =
          let
            key = pkgs.writeTextFile {
              name = "secret-key";
              text = ''
                substituter:SerxxAca5NEsYY0DwVo+subokk+OoHcD9m6JwuctzHgSQVfGHe6nCc+NReDjV3QdFYPMGix4FMg0+K/TM1B3aA==
              '';
            };
          in
          "${key}";
      };
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
        substituters = lib.mkForce [ "http://substituter:5000" ];
        trusted-public-keys = lib.mkForce [ "substituter:EkFXxh3upwnPjUXg41d0HRWDzBoseBTINPiv0zNQd2g=" ];
      };
    };

  testScript =
    { nodes }: # python
    ''
      import json

      start_all()

      substituter.wait_for_unit("multi-user.target")

      ##########################################
      # Test 1: builtins.fetchurl with substitution
      ##########################################

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

      substituter.succeed(f"nix store sign --key-file ${nodes.substituter.services.nix-serve.secretKeyFile} {file_store_path}")

      importer.wait_for_unit("multi-user.target")

      print("Testing fetchurl with substitution...")
      importer.succeed(f"""
        nix-instantiate -vvvvv --eval --json --read-write-mode --expr '
          builtins.fetchurl {{
            url = "file://{missing_file}";
            sha256 = "{file_hash}";
          }}
        '
      """)
      print("✓ fetchurl substitution works!")

      ##########################################
      # Test 2: builtins.fetchTarball with substitution
      ##########################################

      missing_tarball = "/only-on-substituter.tar.gz"

      # Create a directory with some content
      substituter.succeed("""
        mkdir -p /tmp/test-tarball
        echo 'Hello from tarball!' > /tmp/test-tarball/hello.txt
        echo 'Another file' > /tmp/test-tarball/file2.txt
      """)

      # Create a tarball
      substituter.succeed(f"tar czf {missing_tarball} -C /tmp test-tarball")

      # For fetchTarball, we need to first fetch it without hash to get the store path,
      # then compute the NAR hash of that path
      tarball_store_path_json = substituter.succeed(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTarball {{
            url = "file://{missing_tarball}";
          }}
        '
      """)

      tarball_store_path = json.loads(tarball_store_path_json)

      # Get the NAR hash of the unpacked tarball in SRI format
      path_info_json = substituter.succeed(f"nix path-info --json {tarball_store_path}").strip()
      path_info_dict = json.loads(path_info_json)
      # nix path-info returns a dict with store paths as keys
      tarball_hash_sri = path_info_dict[tarball_store_path]["narHash"]
      print(f"Tarball NAR hash (SRI): {tarball_hash_sri}")

      # Also get the old format hash for fetchTarball (which uses sha256 parameter)
      tarball_hash = substituter.succeed(f"nix-store --query --hash {tarball_store_path}").strip()

      # Sign the tarball's store path
      substituter.succeed(f"nix store sign --recursive --key-file ${nodes.substituter.services.nix-serve.secretKeyFile} {tarball_store_path}")

      # Now try to fetch the same tarball on the importer
      # The file doesn't exist locally, so it should be substituted
      print("Testing fetchTarball with substitution...")
      result = importer.succeed(f"""
        nix-instantiate -vvvvv --eval --json --read-write-mode --expr '
          builtins.fetchTarball {{
            url = "file://{missing_tarball}";
            sha256 = "{tarball_hash}";
          }}
        '
      """)

      result_path = json.loads(result)
      print(f"✓ fetchTarball substitution works! Result: {result_path}")

      # Verify the content is correct
      # fetchTarball strips the top-level directory if there's only one
      content = importer.succeed(f"cat {result_path}/hello.txt").strip()
      assert content == "Hello from tarball!", f"Content mismatch: {content}"
      print("✓ fetchTarball content verified!")

      ##########################################
      # Test 3: Verify fetchTree does NOT substitute (preserves metadata)
      ##########################################

      print("Testing that fetchTree without __final does NOT use substitution...")

      # fetchTree with just narHash (not __final) should try to download, which will fail
      # since the file doesn't exist on the importer
      exit_code = importer.fail(f"""
        nix-instantiate --eval --json --read-write-mode --expr '
          builtins.fetchTree {{
            type = "tarball";
            url = "file:///only-on-substituter.tar.gz";
            narHash = "{tarball_hash_sri}";
          }}
        ' 2>&1
      """)

      # Should fail with "does not exist" since it tries to download instead of substituting
      assert "does not exist" in exit_code or "Couldn't open file" in exit_code, f"Expected download failure, got: {exit_code}"
      print("✓ fetchTree correctly does NOT substitute non-final inputs!")
      print("  (This preserves metadata like lastModified from the actual fetch)")
    '';
}
