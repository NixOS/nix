{
  name = "testsOnNixOS-user";

  imports = [ ./testsOnNixOS.nix ];

  nodes.machine = {
    users.users.alice = { isNormalUser = true; };
  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      su --login --command "run-test-suite" alice &>/dev/console
    """)
  '';
}
