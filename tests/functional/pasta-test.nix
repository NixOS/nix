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
