{
  config,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # Test packages - minimal packages for fast copying
  pkgA = pkgs.writeText "test-package-a" "test package a";
  pkgB = pkgs.writeText "test-package-b" "test package b";
  pkgC = pkgs.writeText "test-package-c" "test package c";

  rootDomain = "garage.local";
  s3EndpointPort = 9000;
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
          pkgs.coreutils
        ];
        nix.nixPath = [ "nixpkgs=${pkgs.path}" ];
        nix.extraOptions = ''
          experimental-features = nix-command
          substituters =
        '';
        services.garage = {
          enable = true;
          package = pkgs.garage_2;
          settings = rec {
            replication_factor = 1;
            # Pretty much unused, but required by garage nevertheless.
            rpc_bind_addr = "[::]:3901";
            rpc_public_addr = rpc_bind_addr;
            rpc_secret = "4e440caf9ca89e720ac0c6e78821d52000c670a0944f546535d303162794defc";
            s3_api = {
              api_bind_addr = "[::]:${toString s3EndpointPort}";
              root_domain = rootDomain;
              s3_region = "eu-west-1";
            };
          };
        };
        networking.firewall.allowedTCPPorts = [ s3EndpointPort ];
        networking.extraHosts =
          let
            rootDomain = config.services.garage.settings.s3_api.root_domain;
          in
          "127.0.0.1 vhost-test.${rootDomain} ${rootDomain}";
        environment.systemPackages = [
          pkgs.minio-client
        ];
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
        networking.extraHosts = "192.168.1.2 vhost-test.${rootDomain} ${rootDomain}";
        environment.systemPackages = [
          pkgs.minio-client
        ];
      };
  };

  testScript =
    { nodes }:
    # python
    ''
      from typing import List
      from dataclasses import dataclass
      import json
      import random
      import re
      import uuid

      # ============================================================================
      # Configuration
      # ============================================================================

      ENDPOINT = 'http://server:${toString s3EndpointPort}'
      REGION = 'eu-west-1'
      VHOST_DOMAIN = '${rootDomain}'
      VHOST_ENDPOINT = f'http://{VHOST_DOMAIN}:${toString s3EndpointPort}'

      PKGS = {
          'A': '${pkgA}',
          'B': '${pkgB}',
          'C': '${pkgC}',
      }

      # ============================================================================
      # Helpers for setting up a mock garage single node deployment
      # Copied from nixos/tests/garage/common.nix
      # ============================================================================

      @dataclass
      class S3Key:
         key_name: str
         key_id: str
         secret_key: str

      @dataclass
      class GarageNode:
         node_id: str
         host: str

      def parse_api_key_data(text) -> S3Key:
        key_creation_regex = re.compile(r'Key name: \s*(?P<key_name>.*)|' r'Key ID: \s*(?P<key_id>.*)|' r'Secret key: \s*(?P<secret_key>.*)', re.IGNORECASE)
        fields = {}
        for match in key_creation_regex.finditer(text):
          for key, value in match.groupdict().items():
            if value:
              fields[key] = value.strip()
        try:
          return S3Key(**fields)
        except TypeError as e:
          raise ValueError(f"Cannot parse API key data. Missing required field(s): {e}")

      def create_api_key(machine: BaseMachine, key_name: str) -> S3Key:
         output = machine.succeed(f"garage key create {key_name}")
         return parse_api_key_data(output)

      def get_node_fqn(machine: BaseMachine) -> GarageNode:
        node_id, host = machine.succeed("garage node id").split('@')
        return GarageNode(node_id=node_id, host=host)

      def get_node_id(machine: BaseMachine) -> str:
        return get_node_fqn(machine).node_id

      cur_version_regex = re.compile(r'Current cluster layout version: (?P<ver>\d*)')

      def get_layout_version(machine: BaseMachine) -> int:
        version_data = machine.succeed("garage layout show")
        m = cur_version_regex.search(version_data)
        if m and m.group('ver') is not None:
          return int(m.group('ver')) + 1
        else:
          raise ValueError('Cannot find current layout version')

      def apply_garage_layout(machine: BaseMachine, layouts: List[str]):
         for layout in layouts:
            machine.succeed(f"garage layout assign {layout}")
         version = get_layout_version(machine)
         machine.succeed(f"garage layout apply --version {version}")

      start_all()

      server.wait_for_unit("garage")
      server.wait_for_unit("network-addresses-eth1.service")
      server.wait_for_open_port(${toString s3EndpointPort})
      server.succeed("garage status >&2")
      server_node_id = get_node_id(server)
      apply_garage_layout(server, [f'-z qemutest -c 1G "{server_node_id}"'])
      s3_key = create_api_key(server, "test-api-key")

      ACCESS_KEY = s3_key.key_id
      SECRET_KEY = s3_key.secret_key
      ENV_WITH_CREDS = f"AWS_ACCESS_KEY_ID={ACCESS_KEY} AWS_SECRET_ACCESS_KEY={SECRET_KEY}"

      server.succeed(f"mc alias set garage http://localhost:9000 {ACCESS_KEY} {SECRET_KEY} --api s3v4")

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
          stat = machine.succeed(f"mc stat garage/{bucket}/{object_path}")
          if "Content-Encoding" not in stat or expected_encoding not in stat:
              print(f"mc stat output for {object_path}:")
              print(stat)
              raise Exception(f"Expected Content-Encoding: {expected_encoding} header on {object_path}")

      def verify_no_compression(machine, bucket, object_path):
          """Verify S3 object has no compression headers"""
          stat = machine.succeed(f"mc stat garage/{bucket}/{object_path}")
          if "Content-Encoding" in stat and ("gzip" in stat or "br" in stat):
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

      def setup_s3(populate_bucket=[], public=False, versioned=False, profiles=None):
          """
          Decorator that creates/destroys a unique bucket for each test.
          Optionally pre-populates bucket with specified packages.
          Cleans up client store after test completion.

          Args:
              populate_bucket: List of packages to upload before test runs
              profiles: Dict of AWS profiles to create, e.g.:
                  {"valid": {"access_key": "...", "secret_key": "..."},
                   "invalid": {"access_key": "WRONG", "secret_key": "WRONG"}}
                  Profiles are created on the client machine at /root/.aws/credentials
          """
          def decorator(test_func):
              def wrapper():
                  bucket = str(uuid.uuid4())
                  server.succeed(f"garage bucket create {bucket}")
                  server.succeed(f"garage bucket allow --read --write --owner {bucket} --key test-api-key")
                  try:
                      if profiles:
                          # Build credentials file content
                          creds_content = ""
                          for name, creds in profiles.items():
                              creds_content += f"[{name}]\n"
                              creds_content += f"aws_access_key_id = {creds['access_key']}\n"
                              creds_content += f"aws_secret_access_key = {creds['secret_key']}\n\n"
                          client.succeed("mkdir -p /root/.aws")
                          client.succeed(f"cat > /root/.aws/credentials << 'AWSCREDS'\n{creds_content}AWSCREDS")
                      if populate_bucket:
                          store_url = make_s3_url(bucket)
                          for pkg in populate_bucket:
                              server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {pkg}")
                      test_func(bucket)
                  finally:
                      server.succeed(f"mc rm --force --recursive garage/{bucket}")
                      server.succeed(f"garage bucket delete {bucket} --yes")
                      # Clean up AWS profiles if created
                      if profiles:
                          client.succeed("rm -rf /root/.aws")
                      # Clean up client store - only delete if path exists
                      for pkg in PKGS.values():
                          client.succeed(f"[ ! -e {pkg} ] || nix store delete --ignore-liveness {pkg}")
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

      @setup_s3()
      def test_aws_log_integration(bucket):
          """Test that AWS SDK logs are properly routed through Nix logger"""
          print("\n=== Testing AWS Log Integration ===")

          store_url = make_s3_url(bucket)

          # With default verbosity, AWS noise should NOT appear
          # All AWS messages are demoted to lvlDebug or lvlVomit
          output_default = server.succeed(
              f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['A']} 2>&1"
          )

          if "(aws:" in output_default:
              print("Output at default verbosity:")
              print(output_default)
              raise Exception("Found AWS noise at default verbosity")

          # With --debug (lvlDebug), we should see AWS messages with (aws:subject) prefix
          output_debug = server.succeed(
              f"{ENV_WITH_CREDS} nix copy --debug --to '{store_url}' {PKGS['B']} 2>&1"
          )

          # Check for the (aws:subject) prefix format
          if "(aws:" not in output_debug:
              print("Output at --debug verbosity:")
              print(output_debug)
              raise Exception("Expected to see (aws:subject) prefix in debug output")

          # Should also see Nix's own credential provider creation message
          if "creating new AWS credential provider" not in output_debug:
              print("Debug output:")
              print(output_debug)
              raise Exception("Expected to see credential provider creation at debug level")

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

      @setup_s3(populate_bucket=[PKGS['A']])
      def test_url_format_variations(bucket):
          """Test different S3 URL parameter combinations"""
          print("\n=== Testing URL Format Variations ===")

          # Test parameter order variation (region before endpoint)
          url1 = f"s3://{bucket}?region={REGION}&endpoint={ENDPOINT}"
          client.succeed(f"{ENV_WITH_CREDS} nix store info --store '{url1}' >&2")

          # Test parameter order variation (endpoint before region)
          url2 = f"s3://{bucket}?endpoint={ENDPOINT}&region={REGION}"
          client.succeed(f"{ENV_WITH_CREDS} nix store info --store '{url2}' >&2")

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

          # Verify client can download and decompress
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKGS['B']}")
          verify_packages_in_store(client, PKGS['B'])

      @setup_s3()
      def test_compression_mixed(bucket):
          """Test mixed compression (narinfo=br, ls=gzip)"""
          print("\n=== Testing Compression: mixed (narinfo=br, ls=gzip) ===")

          store_url = make_s3_url(
              bucket,
              **{'narinfo-compression': 'br', 'write-nar-listing': 'true', 'ls-compression': 'gzip'}
          )

          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['C']}")

          pkg_hash = get_package_hash(PKGS['C'])

          # Verify .narinfo has br compression
          verify_content_encoding(server, bucket, f"{pkg_hash}.narinfo", "br")

          # Verify .ls has gzip compression
          verify_content_encoding(server, bucket, f"{pkg_hash}.ls", "gzip")

          # Verify client can download with mixed compression
          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' --no-check-sigs {PKGS['C']}")
          verify_packages_in_store(client, PKGS['C'])

      @setup_s3()
      def test_compression_disabled(bucket):
          """Verify no compression by default"""
          print("\n=== Testing Compression: disabled (default) ===")

          store_url = make_s3_url(bucket)
          server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['A']}")

          pkg_hash = get_package_hash(PKGS['A'])
          verify_no_compression(server, bucket, f"{pkg_hash}.narinfo")

      @setup_s3()
      def test_nix_prefetch_url(bucket):
          """Test that nix-prefetch-url retrieves actual file content from S3, not empty files (issue #8862)"""
          print("\n=== Testing nix-prefetch-url S3 Content Retrieval (issue #8862) ===")

          # Create a test file with known content
          test_content = "This is test content to verify S3 downloads work correctly!\n"
          test_file_size = len(test_content)

          server.succeed(f"echo -n '{test_content}' > /tmp/test-file.txt")

          # Upload to S3
          server.succeed(f"mc cp /tmp/test-file.txt garage/{bucket}/test-file.txt")

          # Calculate expected hash
          expected_hash = server.succeed(
              "nix hash file --type sha256 --base32 /tmp/test-file.txt"
          ).strip()

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

          # Verify the downloaded file is NOT empty (the bug in #8862)
          file_size = int(client.succeed(f"stat -c %s {store_path}").strip())

          if file_size == 0:
              raise Exception("Downloaded file is EMPTY - issue #8862 regression detected!")

          if file_size != test_file_size:
              raise Exception(
                  f"File size mismatch: expected {test_file_size}, got {file_size}"
              )

          # Verify actual content matches by comparing hashes instead of printing entire file
          downloaded_hash = client.succeed(f"nix hash file --type sha256 --base32 {store_path}").strip()

          if downloaded_hash != expected_hash:
              raise Exception(f"Content hash mismatch: expected {expected_hash}, got {downloaded_hash}")

      @setup_s3()
      def test_multipart_upload_basic(bucket):
          """Test basic multipart upload with a large file"""
          print("\n--- Test: Multipart Upload Basic ---")

          large_file_size = 10 * 1024 * 1024
          large_pkg = server.succeed(
              "nix-store --add $(dd if=/dev/urandom of=/tmp/large-file bs=1M count=10 2>/dev/null && echo /tmp/large-file)"
          ).strip()

          chunk_size = 5 * 1024 * 1024
          expected_parts = 3  # 10 MB raw becomes ~10.5 MB compressed (NAR + br overhead)

          store_url = make_s3_url(
              bucket,
              **{
                  "multipart-upload": "true",
                  "multipart-threshold": str(5 * 1024 * 1024),
                  "multipart-chunk-size": str(chunk_size),
              }
          )

          print(f"  Uploading {large_file_size} byte file (expect {expected_parts} parts)")
          output = server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {large_pkg} --debug 2>&1")

          if "using S3 multipart upload" not in output:
              raise Exception("Expected multipart upload to be used")

          expected_msg = f"{expected_parts} parts uploaded"
          if expected_msg not in output:
              print("Debug output:")
              print(output)
              raise Exception(f"Expected '{expected_msg}' in output")

          client.succeed(f"{ENV_WITH_CREDS} nix copy --from '{store_url}' {large_pkg} --no-check-sigs")
          verify_packages_in_store(client, large_pkg, should_exist=True)

      @setup_s3()
      def test_multipart_threshold(bucket):
          """Test that files below threshold use regular upload"""
          print("\n--- Test: Multipart Threshold Behavior ---")

          store_url = make_s3_url(
              bucket,
              **{
                  "multipart-upload": "true",
                  "multipart-threshold": str(1024 * 1024 * 1024),
              }
          )

          print("  Uploading small file with high threshold")
          output = server.succeed(f"{ENV_WITH_CREDS} nix copy --to '{store_url}' {PKGS['A']} --debug 2>&1")

          if "using S3 multipart upload" in output:
              raise Exception("Should not use multipart for file below threshold")

          if "using S3 regular upload" not in output:
              raise Exception("Expected regular upload to be used")

          client.succeed(f"{ENV_WITH_CREDS} nix copy --no-check-sigs --from '{store_url}' {PKGS['A']}")
          verify_packages_in_store(client, PKGS['A'], should_exist=True)

      @setup_s3()
      def test_multipart_with_log_compression(bucket):
          """Test multipart upload with compressed build logs"""
          print("\n--- Test: Multipart Upload with Log Compression ---")

          # Create a derivation that produces a large text log (12 MB of base64 output)
          drv_path = server.succeed(
              """
              nix-instantiate --expr '
                let pkgs = import <nixpkgs> {};
                in derivation {
                  name = "large-log-builder";
                  builder = "/bin/sh";
                  args = ["-c" "$coreutils/bin/dd if=/dev/urandom bs=1M count=12 | $coreutils/bin/base64; echo success > $out"];
                  coreutils = pkgs.coreutils;
                  system = builtins.currentSystem;
                }
              '
              """
          ).strip()

          print("  Building derivation to generate large log")
          server.succeed(f"nix-store --realize {drv_path} &>/dev/null")

          # Upload logs with compression and multipart
          store_url = make_s3_url(
              bucket,
              **{
                  "multipart-upload": "true",
                  "multipart-threshold": str(5 * 1024 * 1024),
                  "multipart-chunk-size": str(5 * 1024 * 1024),
                  "log-compression": "br",
              }
          )

          print("  Uploading build log with compression and multipart")
          output = server.succeed(
              f"{ENV_WITH_CREDS} nix store copy-log --to '{store_url}' {drv_path} --debug 2>&1"
          )

          # Should use multipart for the compressed log
          if "using S3 multipart upload" not in output or "log/" not in output:
              print("Debug output:")
              print(output)
              raise Exception("Expected multipart upload to be used for compressed log")

          if "parts uploaded" not in output:
              print("Debug output:")
              print(output)
              raise Exception("Expected multipart completion message")

      @setup_s3(
          populate_bucket=[PKGS['A']],
          profiles={
              "valid": {"access_key": ACCESS_KEY, "secret_key": SECRET_KEY},
              "invalid": {"access_key": "INVALIDKEY", "secret_key": "INVALIDSECRET"},
          }
      )
      def test_profile_credentials(bucket):
          """Test that profile-based credentials work without environment variables"""
          print("\n=== Testing Profile-Based Credentials ===")

          store_url = make_s3_url(bucket, profile="valid")

          # Verify store info works with profile credentials (no env vars)
          client.succeed(f"HOME=/root nix store info --store '{store_url}' >&2")

          # Verify we can copy from the store using profile
          verify_packages_in_store(client, PKGS['A'], should_exist=False)
          client.succeed(f"HOME=/root nix copy --no-check-sigs --from '{store_url}' {PKGS['A']}")
          verify_packages_in_store(client, PKGS['A'])

          # Clean up the package we just copied so we can test invalid profile
          client.succeed(f"nix store delete --ignore-liveness {PKGS['A']}")
          verify_packages_in_store(client, PKGS['A'], should_exist=False)

          # Verify invalid profile fails when trying to copy
          invalid_url = make_s3_url(bucket, profile="invalid")
          client.fail(f"HOME=/root nix copy --no-check-sigs --from '{invalid_url}' {PKGS['A']} 2>&1")

      @setup_s3(
          populate_bucket=[PKGS['A']],
          profiles={
              "wrong": {"access_key": "WRONGKEY", "secret_key": "WRONGSECRET"},
          }
      )
      def test_env_vars_precedence(bucket):
          """Test that environment variables take precedence over profile credentials"""
          print("\n=== Testing Environment Variables Precedence ===")

          # Use profile with wrong credentials, but provide correct creds via env vars
          store_url = make_s3_url(bucket, profile="wrong")

          # Ensure package is not in client store
          verify_packages_in_store(client, PKGS['A'], should_exist=False)

          # This should succeed because env vars (correct) override profile (wrong)
          output = client.succeed(
              f"HOME=/root {ENV_WITH_CREDS} nix copy --no-check-sigs --debug --from '{store_url}' {PKGS['A']} 2>&1"
          )

          # Verify the credential chain shows Environment provider was added
          if "Added AWS Environment Credential Provider" not in output:
              print("Debug output:")
              print(output)
              raise Exception("Expected Environment provider to be added to chain")

          # Clean up the package so we can test again without env vars
          client.succeed(f"nix store delete --ignore-liveness {PKGS['A']}")
          verify_packages_in_store(client, PKGS['A'], should_exist=False)

          # Without env vars, same URL should fail (proving profile creds are actually wrong)
          client.fail(f"HOME=/root nix copy --no-check-sigs --from '{store_url}' {PKGS['A']} 2>&1")

      @setup_s3(
          populate_bucket=[PKGS['A']],
          profiles={
              "testprofile": {"access_key": ACCESS_KEY, "secret_key": SECRET_KEY},
          }
      )
      def test_credential_provider_chain(bucket):
          """Test that debug logging shows which providers are added to the chain"""
          print("\n=== Testing Credential Provider Chain Logging ===")

          store_url = make_s3_url(bucket, profile="testprofile")

          output = client.succeed(
              f"HOME=/root nix store info --debug --store '{store_url}' 2>&1"
          )

          # For a named profile, we expect to see these providers in the chain
          expected_providers = ["Environment", "Profile", "IMDS"]
          for provider in expected_providers:
              msg = f"Added AWS {provider} Credential Provider to chain for profile 'testprofile'"
              if msg not in output:
                  print("Debug output:")
                  print(output)
                  raise Exception(f"Expected to find: {msg}")

          # SSO should be skipped (no SSO config for this profile)
          if "Skipped AWS SSO Credential Provider for profile 'testprofile'" not in output:
              print("Debug output:")
              print(output)
              raise Exception("Expected SSO provider to be skipped")

      def test_virtual_hosted_copy():
          """Test nix copy with virtual-hosted-style addressing on custom endpoint"""
          print("\n=== Testing Virtual-Hosted-Style Addressing ===")

          # Use a fixed bucket name matching the static /etc/hosts entries
          bucket = 'vhost-test'
          server.succeed(f"garage bucket create {bucket}")
          server.succeed(f"garage bucket allow --read --write --owner {bucket} --key test-api-key")

          try:
              store_url = make_s3_url(
                  bucket,
                  endpoint=VHOST_ENDPOINT,
                  **{'addressing-style': 'virtual'}
              )

              # Upload with virtual-hosted-style, capture debug output
              output = server.succeed(
                  f"{ENV_WITH_CREDS} nix copy --debug --to '{store_url}' {PKGS['A']} 2>&1"
              )

              # Verify virtual-hosted-style URL was used (bucket in hostname)
              vhost_url_prefix = f"http://{bucket}.{VHOST_DOMAIN}:9000/"
              if vhost_url_prefix not in output:
                  print("Debug output:")
                  print(output)
                  raise Exception(
                      f"Expected virtual-hosted-style URL containing '{vhost_url_prefix}'"
                  )

              # Verify path-style URL was NOT used (bucket should not be in the path)
              path_style_pattern = f"{VHOST_ENDPOINT}/{bucket}/"
              if path_style_pattern in output:
                  print("Debug output:")
                  print(output)
                  raise Exception("Found path-style URL when virtual-hosted-style was expected")

              # Download with virtual-hosted-style
              verify_packages_in_store(client, PKGS['A'], should_exist=False)
              output = client.succeed(
                  f"{ENV_WITH_CREDS} nix copy --debug --no-check-sigs "
                  f"--from '{store_url}' {PKGS['A']} 2>&1"
              )

              if vhost_url_prefix not in output:
                  print("Debug output:")
                  print(output)
                  raise Exception(
                      f"Expected virtual-hosted-style URL in download containing '{vhost_url_prefix}'"
                  )

              verify_packages_in_store(client, PKGS['A'])
          finally:
              server.succeed(f"mc rm --force --recursive garage/{bucket}")
              server.succeed(f"garage bucket delete {bucket} --yes")
              for pkg in PKGS.values():
                  client.succeed(f"[ ! -e {pkg} ] || nix store delete --ignore-liveness {pkg}")

      @setup_s3()
      def test_explicit_path_style(bucket):
          """Test that addressing-style=path works as backwards-compatible fallback"""
          print("\n=== Testing Explicit Path-Style Addressing ===")

          store_url = make_s3_url(
              bucket,
              **{'addressing-style': 'path'}
          )

          # Upload with explicit path-style
          output = server.succeed(
              f"{ENV_WITH_CREDS} nix copy --debug --to '{store_url}' {PKGS['A']} 2>&1"
          )

          # Verify path-style URL was used (bucket in path, not hostname)
          path_style_pattern = f"{ENDPOINT}/{bucket}/"
          if path_style_pattern not in output:
              print("Debug output:")
              print(output)
              raise Exception(
                  f"Expected path-style URL containing '{path_style_pattern}'"
              )

          # Download
          verify_packages_in_store(client, PKGS['A'], should_exist=False)
          client.succeed(
              f"{ENV_WITH_CREDS} nix copy --no-check-sigs --from '{store_url}' {PKGS['A']}"
          )
          verify_packages_in_store(client, PKGS['A'])

      # ============================================================================
      # Main Test Execution
      # ============================================================================

      print("\n" + "="*80)
      print("S3 Binary Cache Store Tests")
      print("="*80)

      # Run tests (each gets isolated bucket via decorator)
      test_credential_caching()
      test_aws_log_integration()
      test_fetchurl_basic()
      test_error_message_formatting()
      test_fork_credential_preresolution()
      test_store_operations()
      test_url_format_variations()
      test_concurrent_fetches()
      test_compression_narinfo_gzip()
      test_compression_mixed()
      test_compression_disabled()
      test_nix_prefetch_url()
      test_multipart_upload_basic()
      test_multipart_threshold()
      test_multipart_with_log_compression()
      test_profile_credentials()
      test_env_vars_precedence()
      test_credential_provider_chain()
      test_virtual_hosted_copy()
      test_explicit_path_style()
    '';
}
