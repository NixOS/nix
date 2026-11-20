#!/usr/bin/env bash

source common.sh

# This test requires Linux sandbox support and pasta
needLocalStore "the sandbox only runs on the builder side"
requireSandboxSupport
requiresUnprivilegedUserNamespaces

# Skip test if pasta is not configured or available
PASTA_PATH="${pasta_path:-}"
if [[ -z "$PASTA_PATH" ]] || [[ "$PASTA_PATH" == "pasta" ]] || [[ ! -x "$PASTA_PATH" ]]; then
    skipTest "pasta is not available (pasta_path=$PASTA_PATH)"
fi

# Ensure pasta is in a standard location that Nix can access
# If pasta is in a non-standard location, we need to add it to sandbox-paths
PASTA_DIR=$(dirname "$PASTA_PATH")
export NIX_SANDBOX_PATHS="$PASTA_DIR=$PASTA_DIR"

# Skip test if /dev/net/tun is not available (required for pasta)
if [[ ! -e /dev/net/tun ]]; then
    skipTest "/dev/net/tun not available"
fi

clearStore

# Test that fixed-output derivations can access the network when pasta is enabled
echo 'testing fixed-output derivation with network access...'

# Create a test derivation that tries to access the network
cat > pasta-test.nix <<'EOF'
with import ./config.nix;

{
  # Test basic network functionality with a fixed-output derivation
  testNetworkAccess = mkDerivation {
    name = "test-network-access";
    builder = builtins.toFile "builder.sh" ''
      ${bash}/bin/bash -c '
        # Test basic network connectivity
        # Try to resolve a hostname
        if getent hosts localhost >/dev/null 2>&1; then
          echo "DNS resolution works"
        else
          echo "DNS resolution failed"
          exit 1
        fi

        # Test if we can see network interfaces
        if ${coreutils}/bin/test -e /sys/class/net/eth0; then
          echo "Network interface eth0 exists"
        else
          echo "Network interface eth0 missing"
          exit 1
        fi

        # Create output
        echo "Network test passed" > $out
      '
    '';
    outputHashMode = "flat";
    outputHashAlgo = "sha256";
    outputHash = "sha256-YCa7ssqLHbdFkPJEG4REJJbsZF9g3w1i+Eg21nUYCCk=";
  };

  # Test that non-fixed-output derivations cannot access the network
  testNoNetworkAccess = mkDerivation {
    name = "test-no-network-access";
    builder = builtins.toFile "builder.sh" ''
      ${bash}/bin/bash -c '
        # This should fail because non-fixed-output derivations
        # should not have network access
        if getent hosts localhost >/dev/null 2>&1; then
          echo "ERROR: DNS resolution works but should not!"
          exit 1
        fi

        # There should be no network interfaces
        if ${coreutils}/bin/test -e /sys/class/net/eth0; then
          echo "ERROR: Network interface exists but should not!"
          exit 1
        fi

        echo "Network properly isolated" > $out
      '
    '';
  };
}
EOF

# Test with pasta enabled
echo "Setting pasta-path for network isolation..."
NIX_CONFIG="pasta-path = $PASTA_PATH
sandbox-paths = $NIX_SANDBOX_PATHS" \
  nix-build pasta-test.nix -A testNetworkAccess --no-out-link

# Test that non-fixed-output derivations are still isolated
echo "Testing non-fixed-output derivation isolation..."
nix-build pasta-test.nix -A testNoNetworkAccess --no-out-link

# Test that pasta process is properly cleaned up
echo "Testing pasta process cleanup..."
cat > pasta-cleanup-test.nix <<'EOF'
with import ./config.nix;

mkDerivation {
  name = "pasta-cleanup-test";
  builder = builtins.toFile "builder.sh" ''
    ${bash}/bin/bash -c '
      # Just create output
      echo "test" > $out
    '
  '';
  outputHashMode = "flat";
  outputHashAlgo = "sha256";
  outputHash = "sha256-n4xS51kG4lw0bKJl5VUkJptBS0EbV8LPHZkFV3RJQBU=";
}
EOF

# Build with pasta and check that no pasta processes remain
PASTA_COUNT_BEFORE=$(pgrep -c pasta || echo 0)
NIX_CONFIG="pasta-path = $PASTA_PATH
sandbox-paths = $NIX_SANDBOX_PATHS" \
  nix-build pasta-cleanup-test.nix --no-out-link
PASTA_COUNT_AFTER=$(pgrep -c pasta || echo 0)

if [[ $PASTA_COUNT_AFTER -gt $PASTA_COUNT_BEFORE ]]; then
    echo "ERROR: pasta process was not cleaned up properly"
    exit 1
fi

echo "pasta network isolation tests passed!"
