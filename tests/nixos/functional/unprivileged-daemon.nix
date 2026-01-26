{ lib, ... }:
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

      # nix.enable makes a lot of assumptions that only make sense as root, so set up nix ourselves.
      # Based on nix-daemon.nix from nixpkgs and other references to `config.nix.enable` in nixpkgs.
      nix.enable = false;

      # Unprivileged nix daemon cannot remount store read/write, so never make it read-only in the first place.
      boot.readOnlyNixStore = false;

      environment.systemPackages = [ config.nix.package ];
      # nix normally defaults to local if running as root, we want root to use the daemon as well.
      environment.variables.NIX_REMOTE = "daemon";

      systemd.packages = [ config.nix.package ];

      systemd.services.nix-daemon = {
        path = [
          config.nix.package
          config.programs.ssh.package
        ];
        environment = {
          CURL_CA_BUNDLE = config.security.pki.caBundle;
          NIX_REMOTE = "local?ignore-gc-delete-failure=true";
          NIX_CONFIG = "extra-experimental-features = local-overlay-store";
        };
        serviceConfig = {
          User = "nix-daemon";
          ExecStartPre = "${pkgs.writeShellScript "nix-load-db" ''
            if [[ "$(cat /proc/cmdline)" =~ regInfo=([^ ]*) ]]; then
              ${config.nix.package.out}/bin/nix-store --load-db < ''${BASH_REMATCH[1]}
            fi
          ''}";
        };
      };

      systemd.sockets.nix-daemon.wantedBy = [ "sockets.target" ];

      systemd.tmpfiles.rules = [
        "d  /nix/.rw-store                     0755 nix-daemon nix-daemon - -"
        "d  /nix/store                         0755 nix-daemon nix-daemon - -"
        "d  /nix/store/.links                  0755 nix-daemon nix-daemon - -"
        "d  /nix/var                           0755 nix-daemon nix-daemon - -"
        "d  /nix/var/nix                       0755 nix-daemon nix-daemon - -"
        "d  /nix/var/nix/builds                0755 nix-daemon nix-daemon - -"
        "d  /nix/var/nix/daemon-socket         0755 nix-daemon nix-daemon - -"
        "d  /nix/var/nix/gcroots               0755 nix-daemon nix-daemon - -"
        "L+ /nix/var/nix/gcroots/booted-system 0755 nix-daemon nix-daemon - /run/booted-system"
        "d  /var/empty/.cache/nix              0755 nix-daemon nix-daemon - -"
      ];
    };

  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("""
      su --login --command "run-test-suite" alice >&2
    """)
  '';
}
