{
  lib,
  config,
  nixpkgs,
  pkgs,
  ...
}:

let
  pkg1 = pkgs.go;
in

{
  name = "fsync";

  nodes.machine =
    {
      config,
      lib,
      pkgs,
      ...
    }:
    {
      virtualisation.emptyDiskImages = [ 1024 ];
      environment.systemPackages = [ pkg1 ];
      nix.settings.experimental-features = [ "nix-command" ];
      nix.settings.fsync-store-paths = true;
      nix.settings.require-sigs = false;
      boot.supportedFilesystems = [
        "ext4"
        "btrfs"
        "xfs"
      ];
    };

  testScript =
    { nodes }:
    ''
      # fmt: off
      for fs in ("ext4", "btrfs", "xfs"):
        machine.succeed("mkfs.{} {} /dev/vdb".format(fs, "-F" if fs == "ext4" else "-f"))
        machine.succeed("mkdir -p /mnt")
        machine.succeed("mount /dev/vdb /mnt")
        machine.succeed("sync")
        machine.succeed("nix copy --offline ${pkg1} --to /mnt")
        machine.crash()

        machine.start()
        machine.wait_for_unit("multi-user.target")
        machine.succeed("mkdir -p /mnt")
        machine.succeed("mount /dev/vdb /mnt")
        machine.succeed("nix path-info --offline --store /mnt ${pkg1}")
        machine.succeed("nix store verify --all --store /mnt --no-trust")

        machine.succeed("umount /dev/vdb")
    '';
}
