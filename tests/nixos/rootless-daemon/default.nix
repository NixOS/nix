{ pkgs, ... }:

{
  name = "rootless-daemon";

  nodes.machine = { config, ... }: {
    users.users.alice.isNormalUser = true;
    #users.users.nix-daemon.isSystemUser = true;
    users.users.nix-daemon.isNormalUser = true;
    users.users.nix-daemon.group = "nix-daemon";
    users.groups.nix-daemon = {};
    environment.variables.NIX_REMOTE = "daemon"; # Even for root
    virtualisation.writableStore = true;

    boot.readOnlyNixStore = false;

    # No root daemon
    nix.enable = false;

    # But put Nix on the path anyways
    environment.systemPackages = [ pkgs.nix ];

    # And install the unit fies for nix-gc-trace
    systemd.packages = [ pkgs.nix ];

    # And prepare the socket dirs anyways
    systemd.tmpfiles.rules = [
      "d /nix/var/nix/daemon-socket 0755 nix-daemon root - -"
      "d /nix/var/nix/gc-socket 0755 nix-daemon root - -"
    ];

    # Oops isn't working, because cannot disable Nix daemon and enable
    # Nix settings yet: https://github.com/NixOS/nixpkgs/issues/263250.
    nix.settings = {
      experimental-features = [ "external-gc-daemon" ];
    };
    # Plan B given the above
    #
    # TODO delete once that issue is fixed.
    environment.etc."nix/nix.conf".source = pkgs.writeTextFile {
      name = "nix.conf";
      text = ''
        experimental-features = external-gc-daemon
      '';
    };

    # systemd.user.sockets.nix-daemon = {
    # };
    # systemd.user.services.nix-daemon = {
    #   path = [ pkgs.nix ];
    #   description = "Nix Daemon (non-root)";
    #   unitConfig.ConditionUser = "nix-daemon";
    # };

    systemd.sockets.nix-gc-trace = {
      restartTriggers = [ config.environment.etc."nix/nix.conf".source ];
    };
    systemd.services.nix-gc-trace = {
      restartTriggers = [ config.environment.etc."nix/nix.conf".source ];
      path = [ pkgs.nix ];
      description = "Nix Find Roots";
    };
    # We must enable lingering so that the Systemd User D-Bus is
    # enabled. We also cannot do this with loginctl enable-linger
    # because it needs to happen before systemd is loaded.
    #
    # See https://github.com/NixOS/nixpkgs/issues/3702
    #
    # TODO after upgrading to 23.11 can use new NixOS option for this.
    system.activationScripts = {
      enableLingering = ''
        # remove all existing lingering users
        rm -rf /var/lib/systemd/linger
        mkdir -p /var/lib/systemd/linger
        # enable for the subset of declared users
        touch /var/lib/systemd/linger/nix-daemon
      '';
    };
  };

  testScript = ''
    import re

    machine.wait_for_unit("multi-user.target")

  ''

  # Set up the *user* nix-daemon unit files.
  #
  # TODO Once https://github.com/NixOS/nixpkgs/issues/263248 is fixed we
  # can do this declaratively without reinventing the wheel.
  + ''
    machine.succeed("""
    su --shell=/run/current-system/sw/bin/bash --login nix-daemon -c "$(cat <<'EOF'
      set -ex
      export PS4='+(''${BASH_SOURCE[0]-$0}:$LINENO) '

      src_dir=${pkgs.nix}/lib/systemd/system

      unit_dir=$HOME/.config/systemd/user
      mkdir -p "$unit_dir"
      cd "$unit_dir"

      # Make sure we do not get a "daemon fork bomb" of the daemon
      # trying to connect to itself.
      cp --no-preserve=mode "$src_dir/nix-daemon.service" ./
      patch -p1 < ${./no-systemd-unit-fork-bomb.patch}

      ln -sf "$src_dir/nix-daemon.socket" ./
    EOF
    )"
    """)

    (c, _) = machine.systemctl(
      "daemon-reload",
      user="nix-daemon")
    assert c == 0

  ''

  # Give ownership of the store dir and var to the nix-daemon user.
  #
  # We intentionally don't do `-R` on the store because store objects
  # used by NixOS should still be owned by root.
  + ''
    machine.succeed("""
      set -ex

      chown nix-daemon /nix/store
      chown -R nix-daemon /nix/var
    """)
  ''

  # Test that alice indeed cannot modify the store; we don't want
  # arbitrary users to have any more permissions than before!
  + ''
    machine.fail("su alice -c 'touch /nix/store/foo'")

  ''

  # Start and wait for our units
  + ''
    machine.start_job("nix-gc-trace.socket")
    machine.wait_for_unit("nix-gc-trace.socket")

    machine.start_job("nix-daemon.socket", user="nix-daemon")
    machine.wait_for_unit("nix-daemon.socket", user="nix-daemon")

  ''

  # Create a store obect, remember its store path in Python.
  + ''
    two = machine.succeed("""
    su --login alice -c "$(cat <<'EOF'
      set -ex
      export PS4='+(''${BASH_SOURCE[0]-$0}:$LINENO) '

      echo ehHtmfuULXYyBV6NBk6QUi8iE0 > two
      nix-store --add two
    EOF
    )"
    """)
    two = two.strip()
    assert re.match(r'^/nix/store/.+-two$', two)

  ''

  # Make sure we cannot delete it when it has a GC root, but we can once
  # that root is destroyed.
  + ''
    machine.succeed(f"""
    su --login alice -c "$(cat <<'EOF'
      set -ex
      export PS4='+(''${{BASH_SOURCE[0]-$0}}:$LINENO) '

      test -f {two}
      nix-store --realize {two} --add-root foo
      echo $two
    EOF
    )"
    """)

    machine.fail(f"su --login alice -c 'nix-store --delete {two}'")

    machine.succeed(f"""
    su --login alice -c "$(cat <<'EOF'
      set -ex
      export PS4='+(''${{BASH_SOURCE[0]-$0}}:$LINENO) '

      test -f {two}
      rm foo
      nix-store --delete {two}
      test ! -f {two}
    EOF
    )"
    """)
  '';
}
