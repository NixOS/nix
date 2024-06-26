{
  name = "functional-tests-on-nixos_root";

  imports = [ ./common.nix ];

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      run-test-suite >&2
    """)
  '';
}
