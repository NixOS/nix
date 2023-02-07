{
  name = "testsOnNixOS-root";

  imports = [ ./testsOnNixOS.nix ];

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      run-test-suite &>/dev/console
    """)
  '';
}
