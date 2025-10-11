# Test that Nix doesn't send the same derivation to the daemon multiple times
# during evaluation. This test validates the fix for issue #14006.
#
# The issue: When evaluating expressions with shared dependencies (like bash
# with multiple patches), the client sends the same derivation multiple times
# to the daemon, causing unnecessary overhead.
#
# Run interactively with:
# nix run .#hydraJobs.tests.eval-copy-less.driverInteractive

{
  lib,
  config,
  hostPkgs,
  ...
}:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

in
{
  name = "eval-copy-less";

  nodes.machine =
    {
      config,
      lib,
      pkgs,
      ...
    }:
    {
      virtualisation.writableStore = true;
      nix.settings.experimental-features = [ "nix-command" ];
      environment.systemPackages = [ pkgs.strace ];
    };

  testScript =
    { nodes }:
    ''
      # fmt: off
      import re
      from collections import Counter

      start_all()
      machine.wait_for_unit("nix-daemon.socket")
      machine.wait_for_unit("multi-user.target")

      # Create a Nix expression with shared derivation dependencies
      # Use a random unique identifier to ensure these derivations don't exist yet
      import random
      unique_id = str(random.randint(1000000, 9999999))

      test_nix_content = """
      let
        pkgs = import <nixpkgs> {};

        # Shared base derivation - this should only be sent to daemon once
        # Even though it's referenced by multiple deps below
        base = pkgs.runCommand "test-base-""" + unique_id + """" {} "echo base > $out";

        # Create multiple derivations that all depend on the same base
        # This simulates the bash patches scenario where multiple patches
        # all reference the same bash source tarball
        dep1 = pkgs.runCommand "test-dep1-""" + unique_id + """" {} "cat $${base} > $out; echo dep1 >> $out";
        dep2 = pkgs.runCommand "test-dep2-""" + unique_id + """" {} "cat $${base} > $out; echo dep2 >> $out";
        dep3 = pkgs.runCommand "test-dep3-""" + unique_id + """" {} "cat $${base} > $out; echo dep3 >> $out";
        dep4 = pkgs.runCommand "test-dep4-""" + unique_id + """" {} "cat $${base} > $out; echo dep4 >> $out";
        dep5 = pkgs.runCommand "test-dep5-""" + unique_id + """" {} "cat $${base} > $out; echo dep5 >> $out";

        # Final derivation that depends on all of them
        final = pkgs.runCommand "test-final-""" + unique_id + """" {} "cat $${dep1} $${dep2} $${dep3} $${dep4} $${dep5} > $out";
      in
        final
      """

      machine.succeed("cat > /tmp/test.nix << 'NIXEOF'\n" + test_nix_content + "\nNIXEOF\n")

      # Restart daemon to ensure clean state
      machine.succeed("systemctl restart nix-daemon")
      machine.wait_for_unit("nix-daemon.socket")

      # Run nix-instantiate with strace to capture daemon communication
      # We look for write() calls that contain the Derive( protocol message
      print("Running nix-instantiate with strace...")
      result = machine.succeed("""
        strace -f -e trace=write -s 10000 nix-instantiate /tmp/test.nix 2>&1 | \
        grep 'write([0-9]\\+, "Derive' | \
        cat
      """)

      print("Strace output with Derive() calls:")
      print(result)
      print("=" * 60)

      # Extract all derivation names from the Derive() calls
      # Format: Derive([("out","/nix/store/HASH-NAME",...
      lines = result.strip().split('\n') if result.strip() else []

      # Parse each line to extract the derivation name
      derive_names = []
      for line in lines:
        # Look for store paths in the output
        # Match pattern: /nix/store/HASH-test-NAME-UNIQUEID
        matches = re.findall(r'/nix/store/[a-z0-9]{32}-(test-(?:base|dep\d+|final)-\d+)', line)
        derive_names.extend(matches)

      print(f"Found {len(derive_names)} total derivation sends")
      if derive_names:
          print(f"Sample derivation names: {derive_names[:5]}")

      # Count occurrences of each derivation name
      name_counts = Counter(derive_names)

      # Find the base derivation (should appear once despite being referenced 5 times)
      base_pattern = re.compile(r'test-base-\d+')
      base_sends = [name for name in derive_names if base_pattern.match(name)]

      print(f"Base derivation sends: {len(base_sends)} (should be 1 with fix, >1 without)")
      print(f"Unique derivations: {len(name_counts)}")
      print(f"Total derivation sends: {len(derive_names)}")

      # Count duplicates
      duplicates = {name: count for name, count in name_counts.items() if count > 1}

      if duplicates:
          print("ERROR: Found duplicate derivation sends!")
          for name, count in sorted(duplicates.items()):
              print(f"  {name}: sent {count} times")
          print(f"Total unique: {len(name_counts)}, Total sends: {len(derive_names)}")
          print(f"Unnecessary duplicate sends: {len(derive_names) - len(name_counts)}")

      # The fix should ensure each derivation is sent exactly once
      # Even though writeDerivation() may be called multiple times,
      # addToStoreFromDump() should only be called once per unique derivation
      assert len(duplicates) == 0, \
          f"Found {len(duplicates)} derivations sent multiple times to daemon. " \
          f"Each should be sent only once. " \
          f"Total sends: {len(derive_names)}, Unique: {len(name_counts)}. " \
          f"This indicates the fix in writeDerivation() is not working correctly."

      print("✓ Test passed: All derivations sent exactly once")
    '';
}
