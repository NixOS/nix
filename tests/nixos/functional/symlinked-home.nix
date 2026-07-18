/**
  This test runs the functional tests on a NixOS system where the home directory
  is symlinked to another location that also happens to reside in parent directory
  that we don't have read permissions for (only execute).

  The purpose of this test is to find cases where Nix uses low-level operations
  that don't support symlinks on paths that include them or requires excessive
  permissions for path resolution.

  It is not a substitute for more intricate, use case-specific tests, but helps
  catch common issues.
*/
# TODO: add symlinked tmpdir
{ ... }:
{
  name = "functional-tests-on-nixos_user_symlinked-home";

  imports = [ ./common.nix ];

  nodes.machine = {
    users.users.alice = {
      isNormalUser = true;
    };
  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    with subtest("prepare symlinked home"):
      machine.succeed("""
        (
          set -x
          mkdir -p /home/alice.parent
          chown alice:users /home/alice.parent
          # Make the parent unreadable for good measure
          chmod 0110 /home/alice.parent
          mv /home/alice /home/alice.parent/alice.real
          ln -s alice.parent/alice.real /home/alice
        ) 1>&2
      """)
    machine.succeed("""
      su --login --command "run-test-suite" alice >&2
    """)
  '';
}
