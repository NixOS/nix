{ ... }:
{
  name = "functional-tests-on-nixos_user_symlinked-home";

  imports = [ ./common.nix ];

  nodes.machine = {
    users.users.alice = { isNormalUser = true; };
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
