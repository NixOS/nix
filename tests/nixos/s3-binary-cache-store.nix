{
  lib,
  config,
  nixpkgs,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # Test packages - minimal packages for fast copying
  pkgA = pkgs.writeText "test-package-a" "test package a";
  pkgB = pkgs.writeText "test-package-b" "test package b";
  pkgC = pkgs.writeText "test-package-c" "test package c";

  # S3 configuration
  accessKey = "BKIKJAA5BMMU2RHO6IBB";
  secretKey = "V7f1CwQqAcwo80UEIJEjc5gVQUSSx5ohQ9GSrr12";

in
{
  name = "curl-s3-binary-cache-store";

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
        virtualisation.cores = 2;
        virtualisation.additionalPaths = [
          pkgA
          pkgB
          pkgC
        ];
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
        virtualisation.cores = 2;
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
      import json
      import random
      import re
      import uuid

      # ============================================================================
      # Configuration
      # ============================================================================

      ACCESS_KEY = '${accessKey}'
      SECRET_KEY = '${secretKey}'
      ENDPOINT = 'http://server:9000'
      REGION = 'eu-west-1'

      PKG_A = '${pkgA}'
      PKG_B = '${pkgB}'
      PKG_C = '${pkgC}'

      ENV_WITH_CREDS = f"AWS_ACCESS_KEY_ID={ACCESS_KEY} AWS_SECRET_ACCESS_KEY={SECRET_KEY}"

      # ============================================================================
      # Helper Functions
      # ============================================================================

      def make_s3_url(bucket, path="", **params):
          """Build S3 URL with optional path and query parameters"""
          params.setdefault('endpoint', ENDPOINT)
          params.setdefault('region', REGION)
          query = '&'.join(f"{k}={v}" for k, v in params.items())
          bucket_and_path = f"{bucket}{path}" if path else bucket
          return f"s3://{bucket_and_path}?{query}"

      def make_http_url(path):
          """Build HTTP URL for direct S3 access"""
          return f"{ENDPOINT}/{path}"

      def get_package_hash(pkg_path):
          """Extract store hash from package path"""
          return pkg_path.split("/")[-1].split("-")[0]

      def verify_content_encoding(machine, bucket, object_path, expected_encoding):
          """Verify S3 object has expected Content-Encoding header"""
          stat = machine.succeed(f"mc stat minio/{bucket}/{object_path}")
          if "Content-Encoding" not in stat or expected_encoding not in stat:
              print(f"mc stat output for {object_path}:")
              print(stat)
              raise Exception(f"Expected Content-Encoding: {expected_encoding} header on {object_path}")

      def verify_no_compression(machine, bucket, object_path):
          """Verify S3 object has no compression headers"""
          stat = machine.succeed(f"mc stat minio/{bucket}/{object_path}")
          if "Content-Encoding" in stat and ("gzip" in stat or "xz" in stat):
              print(f"mc stat output for {object_path}:")
              print(stat)
              raise Exception(f"Object {object_path} should not have compression Content-Encoding")

      def assert_count(output, pattern, expected, error_msg):
          """Assert that pattern appears exactly expected times in output"""
          actual = output.count(pattern)
          if actual != expected:
              print("Debug output:")
              print(output)
              raise Exception(f"{error_msg}: expected {expected}, got {actual}")

      def with_test_bucket(populate_with=[]):
          """
          Decorator that creates/destroys a unique bucket for each test.
          Optionally pre-populates bucket with specified packages.

          Args:
              populate_with: List of packages to upload before test runs
          """
          def decorator(test_func):
              def wrapper():
                  bucket = str(uuid.uuid4())
                  server.succeed(f"mc mb minio/{bucket}")
                  try:
                      if populate_with:
                          store_url = make_s3_url(bucket)
                          for pkg in populate_with:
                              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' --no-check-sigs {pkg}")
                      test_func(bucket)
                  finally:
                      server.succeed(f"mc rb --force minio/{bucket}")
              return wrapper
          return decorator

      # ============================================================================
      # Test Functions
      # ============================================================================

      @with_test_bucket()
      def test_credential_caching(bucket):
          """Verify credential providers are cached and reused"""
          print("\n=== Testing Credential Caching ===")

          store_url = make_s3_url(bucket)
          output = server.succeed(
              f"{ENV_WITH_CREDS} nix copy --debug --to '{store_url}' --no-check-sigs "
              f"{PKG_A} {PKG_B} {PKG_C} 2>&1"
          )

          assert_count(
              output,
              "creating new AWS credential provider",
              1,
              "Credential provider caching failed"
          )

          print("✓ Credential provider created once and cached")

      @with_test_bucket(populate_with=[PKG_A])
      def test_fetchurl_basic(bucket):
          """Test builtins.fetchurl works with s3:// URLs"""
          print("\n=== Testing builtins.fetchurl ===")

          client.wait_for_unit("network-addresses-eth1.service")

          cache_info_url = make_s3_url(bucket, path="/nix-cache-info")

          client.succeed(
              f"{ENV_WITH_CREDS} nix eval --impure --expr "
              f"'builtins.fetchurl {{ name = \"foo\"; url = \"{cache_info_url}\"; }}'"
          )

          print("✓ builtins.fetchurl works with s3:// URLs")

      @with_test_bucket()
      def test_error_message_formatting(bucket):
          """Verify error messages display URLs correctly"""
          print("\n=== Testing Error Message Formatting ===")

          nonexistent_url = make_s3_url(bucket, path="/foo-that-does-not-exist")
          expected_http_url = make_http_url(f"{bucket}/foo-that-does-not-exist")

          error_msg = client.fail(
              f"{ENV_WITH_CREDS} nix eval --impure --expr "
              f"'builtins.fetchurl {{ name = \"foo\"; url = \"{nonexistent_url}\"; }}' 2>&1"
          )

          if f"unable to download '{expected_http_url}': HTTP error 404" not in error_msg:
              print("Actual error message:")
              print(error_msg)
              raise Exception("Error message formatting failed - should show actual URL, not %s placeholder")

          print("✓ Error messages format URLs correctly")

      @with_test_bucket(populate_with=[PKG_A])
      def test_fork_credential_preresolution(bucket):
          """Test credential pre-resolution in forked processes"""
          print("\n=== Testing Fork Credential Pre-resolution ===")

          # Get hash of nix-cache-info for fixed-output derivation
          cache_info_url = make_s3_url(bucket, path="/nix-cache-info")

          cache_info_path = client.succeed(
              f"{ENV_WITH_CREDS} nix eval --impure --raw --expr "
              f"'builtins.fetchurl {{ name = \"nix-cache-info\"; url = \"{cache_info_url}\"; }}'"
          ).strip()

          cache_info_hash = client.succeed(
              f"nix hash file --type sha256 --base32 {cache_info_path}"
          ).strip()

          # Build derivation with unique test ID
          test_id = random.randint(0, 10000)
          test_url = make_s3_url(bucket, path="/nix-cache-info", test_id=test_id)

          fetchurl_expr = """
              import <nix/fetchurl.nix> {{
                  name = "s3-fork-test-{id}";
                  url = "{url}";
                  sha256 = "{hash}";
              }}
          """.format(id=test_id, url=test_url, hash=cache_info_hash)

          output = client.succeed(
              f"{ENV_WITH_CREDS} nix build --debug --impure --expr '{fetchurl_expr}' 2>&1"
          )

          # Verify fork behavior
          if "builtin:fetchurl creating fresh FileTransfer instance" not in output:
              print("Debug output:")
              print(output)
              raise Exception("Expected to find FileTransfer creation in forked process")

          print("  ✓ Forked process creates fresh FileTransfer")

          # Verify pre-resolution in parent
          required_messages = [
              "Pre-resolving AWS credentials for S3 URL in builtin:fetchurl",
              "Successfully pre-resolved AWS credentials in parent process",
          ]

          for msg in required_messages:
              if msg not in output:
                  print("Debug output:")
                  print(output)
                  raise Exception(f"Missing expected message: {msg}")

          print("  ✓ Parent pre-resolves credentials")

          # Verify child uses pre-resolved credentials
          if "Using pre-resolved AWS credentials from parent process" not in output:
              print("Debug output:")
              print(output)
              raise Exception("Child should use pre-resolved credentials")

          # Extract child PID and verify it doesn't create new providers
          filetransfer_match = re.search(
              r'\[pid=(\d+)\] builtin:fetchurl creating fresh FileTransfer instance',
              output
          )

          if not filetransfer_match:
              raise Exception("Could not extract child PID from debug output")

          child_pid = filetransfer_match.group(1)
          child_provider_creation = f"[pid={child_pid}] creating new AWS credential provider"

          if child_provider_creation in output:
              print("Debug output:")
              print(output)
              raise Exception(f"Child process (pid={child_pid}) should NOT create new credential providers")

          print("  ✓ Child uses pre-resolved credentials (no new providers)")

      @with_test_bucket(populate_with=[PKG_A, PKG_B, PKG_C])
      def test_store_operations(bucket):
          """Test nix store info and copy operations"""
          print("\n=== Testing Store Operations ===")

          store_url = make_s3_url(bucket)

          # Verify store info works
          client.succeed(f"{ENV_WITH_CREDS} nix store info --store '{store_url}' >&2")

          # Get and validate store info JSON
          info_json = client.succeed(f"{ENV_WITH_CREDS} nix store info --json --store '{store_url}'")
          store_info = json.loads(info_json)

          if not store_info.get("url"):
              raise Exception("Store should have a URL")

          print(f"  ✓ Store URL: {store_info['url']}")

          # Test copy from store
          client.fail(f"nix path-info {PKG_A}")

          output = client.succeed(
              f"{ENV_WITH_CREDS} nix copy --debug --no-check-sigs "
              f"--from '{store_url}' {PKG_A} {PKG_B} {PKG_C} 2>&1"
          )

          assert_count(
              output,
              "creating new AWS credential provider",
              1,
              "Client credential provider caching failed"
          )

          client.succeed(f"nix path-info {PKG_A}")

          print("  ✓ nix copy works")
          print("  ✓ Credentials cached on client")

      @with_test_bucket(populate_with=[PKG_A])
      def test_url_format_variations(bucket):
          """Test different S3 URL parameter combinations"""
          print("\n=== Testing URL Format Variations ===")

          # Test parameter order variation (region before endpoint)
          url1 = f"s3://{bucket}?region={REGION}&endpoint={ENDPOINT}"
          client.succeed(f"{ENV_WITH_CREDS} nix store info --store '{url1}' >&2")
          print("  ✓ Parameter order: region before endpoint works")

          # Test parameter order variation (endpoint before region)
          url2 = f"s3://{bucket}?endpoint={ENDPOINT}&region={REGION}"
          client.succeed(f"{ENV_WITH_CREDS} nix store info --store '{url2}' >&2")
          print("  ✓ Parameter order: endpoint before region works")

      @with_test_bucket(populate_with=[PKG_A])
      def test_concurrent_fetches(bucket):
          """Validate thread safety with concurrent S3 operations"""
          print("\n=== Testing Concurrent Fetches ===")

          # Get hash for test derivations
          cache_info_url = make_s3_url(bucket, path="/nix-cache-info")

          cache_info_path = client.succeed(
              f"{ENV_WITH_CREDS} nix eval --impure --raw --expr "
              f"'builtins.fetchurl {{ name = \"nix-cache-info\"; url = \"{cache_info_url}\"; }}'"
          ).strip()

          cache_info_hash = client.succeed(
              f"nix hash file --type sha256 --base32 {cache_info_path}"
          ).strip()

          # Create 5 concurrent fetch derivations
          # Build base URL for concurrent test (we'll add fetch_id in Nix interpolation)
          base_url = make_s3_url(bucket, path="/nix-cache-info")
          concurrent_expr = """
              let
                  mkFetch = i: import <nix/fetchurl.nix> {{
                      name = "concurrent-s3-fetch-''${{toString i}}";
                      url = "{url}&fetch_id=''${{toString i}}";
                      sha256 = "{hash}";
                  }};
                  fetches = builtins.listToAttrs (map (i: {{
                      name = "fetch''${{toString i}}";
                      value = mkFetch i;
                  }}) (builtins.genList (i: i) 5));
              in fetches
          """.format(url=base_url, hash=cache_info_hash)

          try:
              output = client.succeed(
                  f"{ENV_WITH_CREDS} nix build --debug --impure "
                  f"--expr '{concurrent_expr}' --max-jobs 5 2>&1"
              )
          except:
              output = client.fail(
                  f"{ENV_WITH_CREDS} nix build --debug --impure "
                  f"--expr '{concurrent_expr}' --max-jobs 5 2>&1"
              )

          if "error:" in output.lower():
              print("Found error during concurrent fetches:")
              print(output)

          providers_created = output.count("creating new AWS credential provider")
          transfers_created = output.count("builtin:fetchurl creating fresh FileTransfer instance")

          print(f"  ✓ {providers_created} credential providers created")
          print(f"  ✓ {transfers_created} FileTransfer instances created")

          if transfers_created != 5:
              print("Debug output:")
              print(output)
              raise Exception(
                  f"Expected 5 FileTransfer instances for 5 concurrent fetches, got {transfers_created}"
              )

      @with_test_bucket()
      def test_compression_narinfo_gzip(bucket):
          """Test narinfo compression with gzip"""
          print("\n=== Testing Compression: narinfo (gzip) ===")

          store_url = make_s3_url(bucket, **{'narinfo-compression': 'gzip'})
          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' --no-check-sigs {PKG_B}")

          pkg_hash = get_package_hash(PKG_B)
          verify_content_encoding(server, bucket, f"{pkg_hash}.narinfo", "gzip")

          print("  ✓ .narinfo has Content-Encoding: gzip")

          # Verify client can download and decompress
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKG_B}")
          client.succeed(f"nix path-info {PKG_B}")

          print("  ✓ Client decompressed .narinfo successfully")

      @with_test_bucket()
      def test_compression_mixed(bucket):
          """Test mixed compression (narinfo=xz, ls=gzip)"""
          print("\n=== Testing Compression: mixed (narinfo=xz, ls=gzip) ===")

          store_url = make_s3_url(
              bucket,
              **{'narinfo-compression': 'xz', 'write-nar-listing': 'true', 'ls-compression': 'gzip'}
          )

          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' --no-check-sigs {PKG_C}")

          pkg_hash = get_package_hash(PKG_C)

          # Verify .narinfo has xz compression
          verify_content_encoding(server, bucket, f"{pkg_hash}.narinfo", "xz")
          print("  ✓ .narinfo has Content-Encoding: xz")

          # Verify .ls has gzip compression
          verify_content_encoding(server, bucket, f"{pkg_hash}.ls", "gzip")
          print("  ✓ .ls has Content-Encoding: gzip")

          # Verify client can download with mixed compression
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKG_C}")
          client.succeed(f"nix path-info {PKG_C}")

          print("  ✓ Client downloaded package with mixed compression")

      @with_test_bucket()
      def test_compression_disabled(bucket):
          """Verify no compression by default"""
          print("\n=== Testing Compression: disabled (default) ===")

          store_url = make_s3_url(bucket)
          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' --no-check-sigs {PKG_A}")

          pkg_hash = get_package_hash(PKG_A)
          verify_no_compression(server, bucket, f"{pkg_hash}.narinfo")

          print("  ✓ No compression applied by default")

      @with_test_bucket()
      def test_store_signatures(bucket):
          """Test signature verification for binary cache operations (issue #12491)"""
          print("\n=== Testing Store Signatures ===")

          store_url = make_s3_url(bucket)

          # Generate signing keys
          server.succeed("nix key generate-secret --key-name server-key > /tmp/server.sec")
          server.succeed("nix key convert-secret-to-public < /tmp/server.sec > /tmp/server.pub")
          client.succeed("nix key generate-secret --key-name client-key > /tmp/client.sec")
          client.succeed("nix key convert-secret-to-public < /tmp/client.sec > /tmp/client.pub")

          server_pub_key = server.succeed("cat /tmp/server.pub").strip()
          client_pub_key = client.succeed("cat /tmp/client.pub").strip()

          print(f"  • Server key: {server_pub_key[:50]}...")
          print(f"  • Client key: {client_pub_key[:50]}...")

          # Test 1: Unsigned path should be rejected
          result = server.fail(
              f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKG_A} 2>&1"
          )
          if "is not signed" not in result:
              print("Actual error output:")
              print(result)
              raise Exception("Expected 'is not signed' error for unsigned path")
          print("  ✓ Unsigned path rejected")

          # Test 2: Sign and push with valid signature (server trusts its own key)
          server.succeed(f"nix store sign --key-file /tmp/server.sec {PKG_A}")
          server.succeed(
              f"{ENV_WITH_CREDS} nix copy --to '{store_url}' "
              f"--option trusted-public-keys '{server_pub_key}' {PKG_A}"
          )
          print("  ✓ Signed path accepted")

          # Test 3: Download with trusted key on client
          client.succeed(
              f"{ENV_WITH_CREDS} nix copy --from '{store_url}' "
              f"--option trusted-public-keys '{server_pub_key}' {PKG_A}"
          )
          client.succeed(f"nix path-info {PKG_A}")
          print("  ✓ Signed path downloaded with trusted key")

          # Test 4: Try downloading with wrong trusted key
          client.succeed(f"nix store delete --ignore-liveness {PKG_A}")
          result = client.fail(
              f"{ENV_WITH_CREDS} nix copy --from '{store_url}' "
              f"--option trusted-public-keys '{client_pub_key}' {PKG_A} 2>&1"
          )
          if "signature" not in result.lower():
              raise Exception("Expected signature verification failure with wrong key")
          print("  ✓ Path rejected with wrong trusted key")

          # Test 5: Bypass with --no-check-sigs
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKG_A}")
          print("  ✓ Signature check bypassed with --no-check-sigs")

      # ============================================================================
      # Main Test Execution
      # ============================================================================

      print("\n" + "="*80)
      print("S3 Binary Cache Store Tests")
      print("="*80)

      start_all()

      # Initialize MinIO server
      server.wait_for_unit("minio")
      server.wait_for_unit("network-addresses-eth1.service")
      server.wait_for_open_port(9000)
      server.succeed(f"mc config host add minio http://localhost:9000 {ACCESS_KEY} {SECRET_KEY} --api s3v4")

      # Run tests (each gets isolated bucket via decorator)
      test_credential_caching()
      test_fetchurl_basic()
      test_error_message_formatting()
      test_fork_credential_preresolution()
      test_store_operations()
      test_url_format_variations()
      test_concurrent_fetches()
      test_compression_narinfo_gzip()
      test_compression_mixed()
      test_compression_disabled()
      test_store_signatures()

      print("\n" + "="*80)
      print("✓ All S3 Binary Cache Store Tests Passed!")
      print("="*80)
    '';
}
