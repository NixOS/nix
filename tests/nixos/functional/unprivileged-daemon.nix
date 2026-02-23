{ lib, nixComponents, ... }:

{
  name = "functional-tests-on-nixos_unprivileged-daemon";

  imports = [ ./common.nix ];

  nodes.machine =
    { config, pkgs, ... }:
    {
      users.groups.nix-daemon = { };
      users.users.nix-daemon = {
        isSystemUser = true;
        group = "nix-daemon";
      };
      users.users.alice = {
        isNormalUser = true;
      };

      nix = {
        # We have to use nix-everything for nswrapper, nix-cli doesn't have it.
        package = lib.mkForce nixComponents.nix-everything;
        daemonUser = "nix-daemon";
        daemonGroup = "nix-daemon";
        settings.experimental-features = [
          "local-overlay-store"
          "auto-allocate-uids"
        ];
      };

      # The store setting `ignore-gc-delete-failure` isn't set by default,
      # but is needed since the daemon won't own the entire store.
      systemd.services.nix-daemon.environment.NIX_REMOTE =
        lib.mkForce "local?ignore-gc-delete-failure=true&use-roots-daemon=true";
    };

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      su --login --command "run-test-suite" alice >&2
    """)
  '';
}
