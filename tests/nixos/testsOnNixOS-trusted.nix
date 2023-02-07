{
  name = "testsOnNixOS-trusted";

  imports = [ ./testsOnNixOS.nix ];

  nodes.machine = {
    users.users.alice = { isNormalUser = true; };
    nix.settings.trusted-users = [ "alice" ];
  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      export TEST_TRUSTED_USER=1
      su --login --command "run-test-suite" alice &>/dev/console
    """)
  '';
}
