/**
  This test runs the functional tests on a NixOS system where the home directory
  is symlinked to another location.

  The purpose of this test is to find cases where Nix uses low-level operations
  that don't support symlinks on paths that include them.

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
          mv /home/alice /home/alice.real
          ln -s alice.real /home/alice
        ) 1>&2
      """)
    machine.succeed("""
      su --login --command "run-test-suite" alice >&2
    """)
  '';
}
