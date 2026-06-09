# Regression test for cancelled builds not being reported as failures.
#
# Scenario: When a build fails while other builds are running, those other
# builds (and their dependents) get cancelled. Previously, cancelled builds
# were incorrectly reported as failures with empty error messages.
#
# Uses a fifo for synchronization: fast-fail waits for slow to start before
# failing, ensuring slow is actually running when it gets cancelled.
#
# See: tests/functional/build.sh (search for "cancelled-builds")
{
  outputs =
    { self }:
    let
      config = import "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/config.nix";
    in
    with config;
    {
      checks.${system} = {
        # A derivation that signals it started via fifo, then waits
        slow = mkDerivation {
          name = "slow";
          buildCommand = ''
            echo "slow: started, signaling via fifo"
            echo started > /cancelled-builds-fifo/fifo
            echo "slow: waiting..."
            sleep 10
            touch $out
          '';
        };

        # Depends on slow - will be cancelled when fast-fail fails
        depends-on-slow = mkDerivation {
          name = "depends-on-slow";
          slow = self.checks.${system}.slow;
          buildCommand = ''
            echo "depends-on-slow: slow finished at $slow"
            touch $out
          '';
        };

        # Waits for slow to start via fifo, then fails
        fast-fail = mkDerivation {
          name = "fast-fail";
          buildCommand = ''
            echo "fast-fail: waiting for slow to start..."
            read line < /cancelled-builds-fifo/fifo
            echo "fast-fail: slow started, now failing" >&2
            exit 1
          '';
        };

        # Depends on fast-fail - will fail with DependencyFailed
        depends-on-fail = mkDerivation {
          name = "depends-on-fail";
          fast-fail = self.checks.${system}.fast-fail;
          buildCommand = ''
            echo "depends-on-fail: fast-fail finished (should never get here)"
            touch $out
          '';
        };
      };
    };
}
