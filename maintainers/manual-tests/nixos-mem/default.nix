
{ pkgs ? import ../../../../nixpkgs { system = "x86_64-linux"; } }:

pkgs.runCommand "slow-with-dep-on-nixos" {
  nixos = (pkgs.nixos {
    # make it pass
    boot.loader.grub.enable = false;
    fileSystems."/".device = "x";

    # add some stuff to eval

    services.postgresql.enable = true;

    services.xserver.enable = true;
    services.xserver.displayManager.gdm.enable = true;
    services.xserver.desktopManager.gnome.enable = true;

    programs.git.enable = true;

    system.stateVersion = "24.11"; # shut it up

  }).toplevel;
  note = ''
    sleeping 1h, so you can measure the CLI's resident memory size
    on linux:
        ^Z
        # make sure ps only lists one nix-build process
        grep VmRSS /proc/$(ps | grep nix-build | awk '{print $1}')/status
        fg
        ^C

    sleeping...
  '';
} ''
  echo "$note"

  sleep 1h
''
