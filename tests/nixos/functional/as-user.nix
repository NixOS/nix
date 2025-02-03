{
  name = "functional-tests-on-nixos_user";

  imports = [ ./common.nix ];

  nodes.machine = {
    users.users.alice = {
      isNormalUser = true;
    };
  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      su --login --command "run-test-suite" alice >&2
    """)
  '';
}
