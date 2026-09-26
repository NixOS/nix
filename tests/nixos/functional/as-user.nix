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
    # Regression https://github.com/NixOS/nix/issues/5144
    machine.succeed("""
      su --login alice -c \
        'nix-env -p /nix/var/nix/profiles/system --list-generations'
    """)
  '';
}
