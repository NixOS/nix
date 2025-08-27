{
  lib,
  config,
  nixpkgs,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  pkgA = pkgs.cowsay;

  accessKey = "BKIKJAA5BMMU2RHO6IBB";
  secretKey = "V7f1CwQqAcwo80UEIJEjc5gVQUSSx5ohQ9GSrr12";
  env = "AWS_ACCESS_KEY_ID=${accessKey} AWS_SECRET_ACCESS_KEY=${secretKey}";

  storeUrl = "s3://my-cache?endpoint=http://server:9000&region=eu-west-1";
  objectThatDoesNotExist = "s3://my-cache/foo-that-does-not-exist?endpoint=http://server:9000&region=eu-west-1";

in
{
  name = "s3-binary-cache-store";

  nodes = {
    server =
      {
        config,
        lib,
        pkgs,
        ...
      }:
      {
        virtualisation.writableStore = true;
        virtualisation.additionalPaths = [ pkgA ];
        environment.systemPackages = [ pkgs.minio-client ];
        nix.extraOptions = ''
          experimental-features = nix-command
          substituters =
        '';
        services.minio = {
          enable = true;
          region = "eu-west-1";
          rootCredentialsFile = pkgs.writeText "minio-credentials-full" ''
            MINIO_ROOT_USER=${accessKey}
            MINIO_ROOT_PASSWORD=${secretKey}
          '';
        };
        networking.firewall.allowedTCPPorts = [ 9000 ];
      };

    client =
      { config, pkgs, ... }:
      {
        virtualisation.writableStore = true;
        nix.extraOptions = ''
          experimental-features = nix-command
          substituters =
        '';
      };
  };

  testScript =
    { nodes }:
    # python
    ''
      # fmt: off
      start_all()

      # Create a binary cache.
      server.wait_for_unit("minio")
      server.wait_for_unit("network-addresses-eth1.service")
      server.wait_for_open_port(9000)

      server.succeed("mc config host add minio http://localhost:9000 ${accessKey} ${secretKey} --api s3v4")
      server.succeed("mc mb minio/my-cache")

      # Test copying from a store and credential caching works
      server_cp_out = server.succeed("${env} nix copy --debug --to '${storeUrl}' ${pkgA} 2>&1")
      server_providers_created = server_cp_out.count("creating new AWS credential provider")
      if server_providers_created != 1:
          raise Exception(f"Expected only 1 credential provider to be created, but got {server_providers_created}. Credential provider caching is probably not working.")

      client.wait_for_unit("network-addresses-eth1.service")

      # Test fetchurl on s3:// URLs while we're at it.
      client.succeed("${env} nix eval --impure --expr 'builtins.fetchurl { name = \"foo\"; url = \"s3://my-cache/nix-cache-info?endpoint=http://server:9000&region=eu-west-1\"; }'")

      # Test that the format string in the error message is properly setup and won't display `%s` instead of the failed URI
      msg = client.fail("${env} nix eval --impure --expr 'builtins.fetchurl { name = \"foo\"; url = \"${objectThatDoesNotExist}\"; }' 2>&1")
      if "unable to download '${objectThatDoesNotExist}': HTTP error 404" not in msg:
        print(msg) # So that you can see the message that was improperly formatted
        raise Exception("Error message formatting didn't work")

      # Test derivation-based fetchurl to validate forked process behavior
      # Use a raw derivation with builtin:fetchurl to ensure we can observe fork behavior
      # First, get the actual hash of nix-cache-info
      cache_info_path = client.succeed("${env} nix eval --impure --raw --expr 'builtins.fetchurl { name = \"nix-cache-info\"; url = \"s3://my-cache/nix-cache-info?endpoint=http://server:9000&region=eu-west-1\"; }'")
      cache_info_path = cache_info_path.strip()
      cache_info_hash = client.succeed(f"nix hash file --type sha256 --base32 {cache_info_path}")
      cache_info_hash = cache_info_hash.strip()

      # Use a unique name to avoid cache hits
      import random
      derivation_test = f"""
        derivation {{
          name = "s3-fork-test-{random.randint(0, 100)}";
          builder = "builtin:fetchurl";
          url = "s3://my-cache/nix-cache-info?endpoint=http://server:9000&region=eu-west-1";
          outputHashMode = "flat";
          outputHashAlgo = "sha256";
          outputHash = "{cache_info_hash}";
          system = "x86_64-linux";
        }}
      """

      # Run the derivation and capture debug output to check credential provider creation
      derivation_output = client.succeed("${env} nix build --debug --impure --expr '" + derivation_test + "' 2>&1")

      # Check for evidence of forked process behavior
      if "builtin:fetchurl creating fresh FileTransfer instance" not in derivation_output:
        print("Debug output:")
        print(derivation_output)
        raise Exception("FAILED: Expected to find 'builtin:fetchurl creating fresh FileTransfer instance' in debug output")
      else:
        print("SUCCESS: Found evidence of FileTransfer creation in forked process")

      if "[pid=" not in derivation_output or "creating new AWS credential provider" not in derivation_output:
        print("Debug output:")
        print(derivation_output)
        raise Exception("FAILED: Expected to find PID tracking and credential provider creation in debug output")
      else:
        print("SUCCESS: Found evidence of credential provider creation with PID tracking")

      # Test multiple sequential derivations to check if credential providers are recreated
      sequential_providers_found = 0
      for i in range(3):
        test_expr = f"""
          derivation {{
            name = "s3-fetch-sequential-{i}";
            builder = "builtin:fetchurl";
            url = "s3://my-cache/nix-cache-info?endpoint=http://server:9000&region=eu-west-1";
            outputHashMode = "flat";
            outputHashAlgo = "sha256";
            outputHash = "{cache_info_hash}";
            system = "x86_64-linux";
          }}
        """
        try:
          result = client.succeed("${env} nix build --debug --impure --expr '" + test_expr + "' 2>&1")
        except:
          result = client.fail("${env} nix build --debug --impure --expr '" + test_expr + "' 2>&1")

        if "builtin:fetchurl creating fresh FileTransfer instance" in result:
          sequential_providers_found += 1
          print(f"Derivation {i}: Found FileTransfer creation in forked process")

        if "[pid=" in result:
          pids = [line for line in result.split('\\n') if 'creating new AWS credential provider' in line]
          if pids:
            print(f"Derivation {i}: Found credential provider creation: {pids[0]}")

      # Each derivation should create its own FileTransfer in a forked process
      if sequential_providers_found != 3:
        raise Exception(f"FAILED: Expected 3 FileTransfer creations in sequential derivations, but found {sequential_providers_found}")

      # Copy a package from the binary cache.
      client.fail("nix path-info ${pkgA}")

      # Test nix store info with various S3 URL formats
      client.succeed("${env} nix store info --store '${storeUrl}' >&2")

      # Test with different region parameter (should work without warnings)
      client.succeed("${env} nix store info --store 's3://my-cache?endpoint=http://server:9000&region=us-east-2' >&2")

      # Test with just bucket name and region
      client.succeed("${env} nix store info --store 's3://my-cache?region=eu-west-1&endpoint=http://server:9000' >&2")

      # Test that store info shows the store is available
      info = client.succeed("${env} nix store info --json --store '${storeUrl}'")
      import json
      store_info = json.loads(info)
      assert store_info.get("url"), "Store should have a URL"
      print(f"Store URL: {store_info.get('url')}")

      # Test copying from a store and credential caching works
      client_cp_out = client.succeed("${env} nix copy --debug --no-check-sigs --from '${storeUrl}' ${pkgA} 2>&1")
      client_providers_created = client_cp_out.count("creating new AWS credential provider")
      if client_providers_created != 1:
          raise Exception(f"Expected only 1 credential provider to be created, but got {client_providers_created}. Credential provider caching is probably not working.")

      client.succeed("nix path-info ${pkgA}")

      # Test concurrent S3 fetches to identify potential thread safety issues
      print("Testing concurrent S3 fetches...")

      # Create multiple concurrent derivations that fetch from S3
      # Use raw derivations to ensure we can observe fork behavior
      concurrent_test = """
        let
          mkFetch = i: derivation {
            name = "concurrent-s3-fetch-''${toString i}";
            builder = "builtin:fetchurl";
            url = "s3://my-cache/nix-cache-info?endpoint=http://server:9000&region=eu-west-1";
            outputHashMode = "flat";
            outputHashAlgo = "sha256";
            outputHash = \"""" + cache_info_hash + """\";
            system = "x86_64-linux";
          };
          # Create 5 concurrent fetches
          fetches = builtins.listToAttrs (map (i: {
            name = "fetch''${toString i}";
            value = mkFetch i;
          }) (builtins.genList (i: i) 5));
        in fetches
      """

      # Build all derivations concurrently with debug output
      try:
        concurrent_output = client.succeed("${env} nix build --debug --impure --expr '" + concurrent_test + "' --max-jobs 5 2>&1")
      except:
        concurrent_output = client.fail("${env} nix build --debug --impure --expr '" + concurrent_test + "' --max-jobs 5 2>&1")

      # Check for any errors or crashes
      if "error:" in concurrent_output.lower():
        print("Found error during concurrent fetches:")
        print(concurrent_output)
        # Don't fail immediately, but log the issue

      # Count how many credential providers were created
      concurrent_providers = concurrent_output.count("creating new AWS credential provider")
      concurrent_transfers = concurrent_output.count("builtin:fetchurl creating fresh FileTransfer instance")

      print(f"Concurrent test: {concurrent_providers} credential providers created")
      print(f"Concurrent test: {concurrent_transfers} FileTransfer instances created")

      # Each forked process should create its own FileTransfer
      if concurrent_transfers != 5:
        print("Debug output:")
        print(concurrent_output)
        raise Exception(f"FAILED: Expected at least 5 FileTransfer instances for 5 concurrent fetches, but got {concurrent_transfers}")
    '';
}
