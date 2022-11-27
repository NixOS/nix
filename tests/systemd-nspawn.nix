{ nixpkgs }:

let

  machine = { config, pkgs, ... }:
    {
      system.stateVersion = "22.05";
      boot.isContainer = true;
      systemd.services.console-getty.enable = false;
      networking.dhcpcd.enable = false;

      services.httpd = {
        enable = true;
        adminAddr = "nixos@example.org";
      };

      systemd.services.test = {
        wantedBy = [ "multi-user.target" ];
        after = [ "httpd.service" ];
        script = ''
          source /.env
          echo "Hello World" > $out/msg
          ls -lR /dev > $out/dev
          ${pkgs.curl}/bin/curl -sS --fail http://localhost/ > $out/page.html
        '';
        unitConfig = {
          FailureAction = "exit-force";
          FailureActionExitStatus = 42;
          SuccessAction = "exit-force";
        };
      };
    };

  cfg = (import (nixpkgs + "/nixos/lib/eval-config.nix") {
    modules = [ machine ];
    system = "x86_64-linux";
  });

  config = cfg.config;

in

with cfg._module.args.pkgs;

runCommand "test"
  { buildInputs = [ config.system.path ];
    requiredSystemFeatures = [ "uid-range" ];
    toplevel = config.system.build.toplevel;
  }
  ''
    root=$(pwd)/root
    mkdir -p $root $root/etc

    export > $root/.env

    # Make /run a tmpfs to shut up a systemd warning.
    mkdir /run
    mount -t tmpfs none /run
    chmod 0700 /run

    mount -t cgroup2 none /sys/fs/cgroup

    mkdir -p $out

    touch /etc/os-release
    echo a5ea3f98dedc0278b6f3cc8c37eeaeac > /etc/machine-id

    SYSTEMD_NSPAWN_UNIFIED_HIERARCHY=1 \
      ${config.systemd.package}/bin/systemd-nspawn \
      --keep-unit \
      -M ${config.networking.hostName} -D "$root" \
      --register=no \
      --resolv-conf=off \
      --bind-ro=/nix/store \
      --bind=$out \
      --private-network \
      $toplevel/init
  ''
