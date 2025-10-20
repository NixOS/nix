{
  lib,
  config,
  nixpkgs,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  testPkgs = lib.genAttrs [
    "A"
    "B"
    "C"
    "D"
    "E"
    "F"
    "G"
  ] (n: pkgs.writeText "test-package-${n}" "test package ${n}");

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
        virtualisation.additionalPaths = lib.attrValues testPkgs;
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

      PKGS = json.loads('${builtins.toJSON testPkgs}')

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

      def verify_packages_in_store(machine, pkg_paths, should_exist=True):
          """
          Verify whether packages exist in the store.

          Args:
              machine: The machine to check on
              pkg_paths: List of package paths to check (or single path)
              should_exist: If True, verify packages exist; if False, verify they don't
          """
          paths = [pkg_paths] if isinstance(pkg_paths, str) else pkg_paths
          for pkg in paths:
              if should_exist:
                  machine.succeed(f"nix path-info {pkg}")
              else:
                  machine.fail(f"nix path-info {pkg}")

      def setup_s3(populate_bucket=[], public=False, required_signatures=[], require_all_signatures=False):
          """
          Decorator that creates/destroys a unique bucket for each test.
          Optionally pre-populates bucket with specified packages.
          Cleans up client store after test completion.

          Args:
              populate_bucket: List of packages to upload before test runs
              public: If True, make the bucket publicly accessible
              required_signatures: List of public keys to require in nix-cache-info
              require_all_signatures: If True, require ALL signatures (not just one)
          """
          def decorator(test_func):
              def wrapper():
                  bucket = str(uuid.uuid4())
                  server.succeed(f"mc mb minio/{bucket}")
                  if public:
                      server.succeed(f"mc anonymous set download minio/{bucket}")

                  # Upload nix-cache-info with optional RequiredSignatures
                  cache_info = "StoreDir: /nix/store"
                  if required_signatures:
                      sigs = " ".join(required_signatures)
                      cache_info += f"\\nRequiredSignatures: {sigs}"
                      if require_all_signatures:
                          cache_info += "\\nRequireAllSignatures: 1"
                  server.succeed(f"echo -e '{cache_info}' | mc pipe minio/{bucket}/nix-cache-info")

                  try:
                      if populate_bucket:
                          store_url = make_s3_url(bucket)
                          for pkg in populate_bucket:
                              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {pkg}")
                      test_func(bucket)
                  finally:
                      server.succeed(f"mc rb --force minio/{bucket}")
                      # Surprisingly, nix store delete doesn't care if a path does not exist at all
                      client.succeed(f'nix store delete --ignore-liveness {" ".join(PKGS.values())}')
              return wrapper
          return decorator

      # ============================================================================
      # Test Functions
      # ============================================================================

      @setup_s3()
      def test_credential_caching(bucket):
          """Verify credential providers are cached and reused"""
          print("\n=== Testing Credential Caching ===")

          store_url = make_s3_url(bucket)
          output = server.succeed(
              f"{ENV_WITH_CREDS} nix copy --debug --to '{store_url}' "
              f"{PKGS['A']} {PKGS['B']} {PKGS['C']} 2>&1"
          )

          assert_count(
              output,
              "creating new AWS credential provider",
              1,
              "Credential provider caching failed"
          )

          print("✓ Credential provider created once and cached")

      @setup_s3(populate_bucket=[PKGS['A']])
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

      @setup_s3()
      def test_error_message_formatting(bucket):
          """Verify error messages display URLs correctly"""
          print("\n=== Testing Error Message Formatting ===")

          nonexistent_url = make_s3_url(bucket, path="/foo-that-does-not-exist")
          expected_http_url = f"{ENDPOINT}/{bucket}/foo-that-does-not-exist"

          error_msg = client.fail(
              f"{ENV_WITH_CREDS} nix eval --impure --expr "
              f"'builtins.fetchurl {{ name = \"foo\"; url = \"{nonexistent_url}\"; }}' 2>&1"
          )

          if f"unable to download '{expected_http_url}': HTTP error 404" not in error_msg:
              print("Actual error message:")
              print(error_msg)
              raise Exception("Error message formatting failed - should show actual URL, not %s placeholder")

          print("✓ Error messages format URLs correctly")

      @setup_s3(populate_bucket=[PKGS['A']])
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
              f"{ENV_WITH_CREDS} nix build --debug --impure --no-link --expr '{fetchurl_expr}' 2>&1"
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

      @setup_s3(populate_bucket=[PKGS['A'], PKGS['B'], PKGS['C']])
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
          verify_packages_in_store(client, PKGS['A'], should_exist=False)

          output = client.succeed(
              f"{ENV_WITH_CREDS} nix copy --debug --no-check-sigs "
              f"--from '{store_url}' {PKGS['A']} {PKGS['B']} {PKGS['C']} 2>&1"
          )

          assert_count(
              output,
              "creating new AWS credential provider",
              1,
              "Client credential provider caching failed"
          )

          verify_packages_in_store(client, [PKGS['A'], PKGS['B'], PKGS['C']])

          print("  ✓ nix copy works")
          print("  ✓ Credentials cached on client")

      @setup_s3(populate_bucket=[PKGS['A'], PKGS['B']], public=True)
      def test_public_bucket_operations(bucket):
          """Test store operations on public bucket without credentials"""
          print("\n=== Testing Public Bucket Operations ===")

          store_url = make_s3_url(bucket)

          # Verify store info works without credentials
          client.succeed(f"nix store info --store '{store_url}' >&2")
          print("  ✓ nix store info works without credentials")

          # Get and validate store info JSON
          info_json = client.succeed(f"nix store info --json --store '{store_url}'")
          store_info = json.loads(info_json)

          if not store_info.get("url"):
              raise Exception("Store should have a URL")

          print(f"  ✓ Store URL: {store_info['url']}")

          # Verify packages are not yet in client store
          verify_packages_in_store(client, [PKGS['A'], PKGS['B']], should_exist=False)

          # Test copy from public bucket without credentials
          client.succeed(
              f"nix copy --debug --no-check-sigs "
              f"--from '{store_url}' {PKGS['A']} {PKGS['B']} 2>&1"
          )

          # Verify packages were copied successfully
          verify_packages_in_store(client, [PKGS['A'], PKGS['B']])

          print("  ✓ nix copy from public bucket works without credentials")

      @setup_s3(populate_bucket=[PKGS['A']])
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

      @setup_s3(populate_bucket=[PKGS['A']])
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
                  f"{ENV_WITH_CREDS} nix build --debug --impure --no-link "
                  f"--expr '{concurrent_expr}' --max-jobs 5 2>&1"
              )
          except:
              output = client.fail(
                  f"{ENV_WITH_CREDS} nix build --debug --impure --no-link "
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

          if providers_created != 1:
              print("Debug output:")
              print(output)
              raise Exception(
                  f"Expected 1 credential provider for concurrent fetches, got {providers_created}"
              )

      @setup_s3()
      def test_compression_narinfo_gzip(bucket):
          """Test narinfo compression with gzip"""
          print("\n=== Testing Compression: narinfo (gzip) ===")

          store_url = make_s3_url(bucket, **{'narinfo-compression': 'gzip'})
          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['B']}")

          pkg_hash = get_package_hash(PKGS['B'])
          verify_content_encoding(server, bucket, f"{pkg_hash}.narinfo", "gzip")

          print("  ✓ .narinfo has Content-Encoding: gzip")

          # Verify client can download and decompress
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKGS['B']}")
          verify_packages_in_store(client, PKGS['B'])

          print("  ✓ Client decompressed .narinfo successfully")

      @setup_s3()
      def test_compression_mixed(bucket):
          """Test mixed compression (narinfo=xz, ls=gzip)"""
          print("\n=== Testing Compression: mixed (narinfo=xz, ls=gzip) ===")

          store_url = make_s3_url(
              bucket,
              **{'narinfo-compression': 'xz', 'write-nar-listing': 'true', 'ls-compression': 'gzip'}
          )

          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['C']}")

          pkg_hash = get_package_hash(PKGS['C'])

          # Verify .narinfo has xz compression
          verify_content_encoding(server, bucket, f"{pkg_hash}.narinfo", "xz")
          print("  ✓ .narinfo has Content-Encoding: xz")

          # Verify .ls has gzip compression
          verify_content_encoding(server, bucket, f"{pkg_hash}.ls", "gzip")
          print("  ✓ .ls has Content-Encoding: gzip")

          # Verify client can download with mixed compression
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKGS['C']}")
          verify_packages_in_store(client, PKGS['C'])

          print("  ✓ Client downloaded package with mixed compression")

      @setup_s3()
      def test_compression_disabled(bucket):
          """Verify no compression by default"""
          print("\n=== Testing Compression: disabled (default) ===")

          store_url = make_s3_url(bucket)
          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['A']}")

          pkg_hash = get_package_hash(PKGS['A'])
          verify_no_compression(server, bucket, f"{pkg_hash}.narinfo")

          print("  ✓ No compression applied by default")

      @setup_s3()
      def test_nix_prefetch_url(bucket):
          """Test that nix-prefetch-url retrieves actual file content from S3, not empty files (issue #8862)"""
          print("\n=== Testing nix-prefetch-url S3 Content Retrieval (issue #8862) ===")

          # Create a test file with known content
          test_content = "This is test content to verify S3 downloads work correctly!\n"
          test_file_size = len(test_content)

          server.succeed(f"echo -n '{test_content}' > /tmp/test-file.txt")

          # Upload to S3
          server.succeed(f"mc cp /tmp/test-file.txt minio/{bucket}/test-file.txt")

          # Calculate expected hash
          expected_hash = server.succeed(
              "nix hash file --type sha256 --base32 /tmp/test-file.txt"
          ).strip()

          print(f"  ✓ Uploaded test file to S3 ({test_file_size} bytes)")

          # Use nix-prefetch-url to download from S3
          s3_url = make_s3_url(bucket, path="/test-file.txt")

          prefetch_output = client.succeed(
              f"{ENV_WITH_CREDS} nix-prefetch-url --print-path '{s3_url}'"
          )

          # Extract hash and store path
          # With --print-path, output is: <hash>\n<store-path>
          lines = prefetch_output.strip().split('\n')
          prefetch_hash = lines[0]  # First line is the hash
          store_path = lines[1]  # Second line is the store path

          # Verify hash matches
          if prefetch_hash != expected_hash:
              raise Exception(
                  f"Hash mismatch: expected {expected_hash}, got {prefetch_hash}"
              )

          print("  ✓ nix-prefetch-url completed with correct hash")

          # Verify the downloaded file is NOT empty (the bug in #8862)
          file_size = int(client.succeed(f"stat -c %s {store_path}").strip())

          if file_size == 0:
              raise Exception("Downloaded file is EMPTY - issue #8862 regression detected!")

          if file_size != test_file_size:
              raise Exception(
                  f"File size mismatch: expected {test_file_size}, got {file_size}"
              )

          print(f"  ✓ File has correct size ({file_size} bytes, not empty)")

          # Verify actual content matches by comparing hashes instead of printing entire file
          downloaded_hash = client.succeed(f"nix hash file --type sha256 --base32 {store_path}").strip()

          if downloaded_hash != expected_hash:
              raise Exception(f"Content hash mismatch: expected {expected_hash}, got {downloaded_hash}")

          print("  ✓ File content verified correct (hash matches)")

      def test_required_signatures():
          """Test RequiredSignatures field enforcement"""
          print("\n=== Testing RequiredSignatures ===")

          # Generate signing keys
          server.succeed("nix key generate-secret --key-name cache.example.org > /tmp/sk1")
          server.succeed("nix key generate-secret --key-name other.example.org > /tmp/sk2")

          pk1 = server.succeed("nix key convert-secret-to-public < /tmp/sk1").strip()
          pk2 = server.succeed("nix key convert-secret-to-public < /tmp/sk2").strip()

          # Test 1: Unsigned paths are rejected
          @setup_s3(required_signatures=[pk1])
          def test_unsigned_rejected(bucket):
              store_url = make_s3_url(bucket)
              error = server.fail(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['D']} 2>&1")
              if "refusing to upload unsigned path" not in error:
                  raise Exception("Expected error about unsigned path")
              print("  ✓ Unsigned paths rejected")

          # Test 2: Correctly signed paths are accepted
          @setup_s3(required_signatures=[pk1])
          def test_signed_accepted(bucket):
              server.succeed(f"nix store sign --key-file /tmp/sk1 {PKGS['E']}")
              store_url = make_s3_url(bucket)
              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['E']}")
              print("  ✓ Correctly signed paths accepted")

          # Test 3: Wrong key signatures are rejected
          @setup_s3(required_signatures=[pk1])
          def test_wrong_key_rejected(bucket):
              server.succeed(f"nix store sign --key-file /tmp/sk2 {PKGS['F']}")
              store_url = make_s3_url(bucket)
              error = server.fail(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['F']} 2>&1")
              if "not by any of the keys this cache requires" not in error:
                  raise Exception("Expected error about wrong key")
              print("  ✓ Wrong key signatures rejected")

          # Test 4: Content-addressed paths don't need signatures
          @setup_s3(required_signatures=[pk1])
          def test_ca_paths(bucket):
              # Convert an existing package to content-addressed
              ca_output = server.succeed(f"nix store make-content-addressed --json {PKGS['A']}")
              ca_info = json.loads(ca_output)
              ca_path = ca_info["rewrites"][PKGS['A']]

              store_url = make_s3_url(bucket)
              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {ca_path}")
              print("  ✓ Content-addressed paths work without signatures")

          # Test 5: Multiple required keys (any one is sufficient)
          @setup_s3(required_signatures=[pk1, pk2])
          def test_multiple_keys(bucket):
              # Path signed with sk1 should succeed
              server.succeed(f"nix store sign --key-file /tmp/sk1 {PKGS['B']}")
              store_url = make_s3_url(bucket)
              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['B']}")

              # Path signed with sk2 should also succeed
              server.succeed(f"nix store sign --key-file /tmp/sk2 {PKGS['C']}")
              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['C']}")
              print("  ✓ Multiple required keys work (any one is sufficient)")

          # Test 6: RequireAllSignatures enforcement
          @setup_s3(required_signatures=[pk1, pk2], require_all_signatures=True)
          def test_require_all_signatures(bucket):
              store_url = make_s3_url(bucket)

              # Path signed with only pk1 should fail
              server.succeed(f"nix store sign --key-file /tmp/sk1 {PKGS['G']}")
              error = server.fail(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['G']} 2>&1")
              if "ALL of these keys" not in error or "1 out of 2 required" not in error:
                  raise Exception(f"Expected error about ALL keys and signature count. Got: {error}")

              # Path signed with both pk1 and pk2 should succeed
              server.succeed(f"nix store sign --key-file /tmp/sk2 {PKGS['G']}")
              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['G']}")
              print("  ✓ RequireAllSignatures enforces all keys must sign")

          # Run all sub-tests
          test_unsigned_rejected()
          test_signed_accepted()
          test_wrong_key_rejected()
          test_ca_paths()
          test_multiple_keys()
          test_require_all_signatures()

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
      test_public_bucket_operations()
      test_url_format_variations()
      test_concurrent_fetches()
      test_compression_narinfo_gzip()
      test_compression_mixed()
      test_compression_disabled()
      test_nix_prefetch_url()
      test_required_signatures()

      print("\n" + "="*80)
      print("✓ All S3 Binary Cache Store Tests Passed!")
      print("="*80)
    '';
}
