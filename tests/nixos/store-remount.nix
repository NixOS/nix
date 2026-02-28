# Test that `makeStoreWritable` doesn't fail with EPERM when inherited
# mount flags like `nodev` are locked in a user namespace
# (https://github.com/NixOS/nix/issues/9705).

{ lib, ... }:

{
  name = "store-remount";

  nodes.machine =
    { pkgs, ... }:
    {
      virtualisation.writableStore = true;
      virtualisation.additionalPaths = [
        pkgs.nix
        pkgs.util-linux
        pkgs.strace
        pkgs.gnugrep
        pkgs.bash
      ];
      nix.settings.substituters = lib.mkForce [ ];
    };

  testScript =
    { nodes }:
    let
      pkgs = nodes.machine.nixpkgs.pkgs;
      nix = nodes.machine.nix.package;
    in
    ''
      machine.wait_for_unit("multi-user.target")

      # Add nodev to the host store mount so it becomes locked in the
      # user namespace below.
      machine.succeed("mount -o remount,bind,rw,nodev /nix/store")

      # Set up a minimal container root (which needs its own db to avoid
      # UID-mapping issues on the host db).
      machine.succeed("mkdir -p /tmp/container/{nix/store,nix/var/nix/db,tmp,etc,proc,sys}")
      machine.succeed("echo 'root:x:0:0:root:/root:/bin/sh' > /tmp/container/etc/passwd")
      machine.succeed("echo 'root:x:0:' > /tmp/container/etc/group")

      # In a user namespace (`--private-users`), inherited flags like nodev
      # are locked. We make the store read-only, then let `nix store info`
      # trigger `makeStoreWritable`. Without the fix, the remount fails
      # with EPERM because the old code implicitly dropped locked flags.
      #
      # We use strace to capture the mount() syscall and verify that
      # MS_NODEV is present in the flags, since nix does
      # unshare(CLONE_NEWNS) internally so the remount is not observable
      # from outside the nix process.
      machine.succeed(
          "systemd-nspawn --quiet --register=no --private-network "
          "--private-users=pick "
          "-D /tmp/container "
          "--bind=/nix/store "
          "--as-pid2 "
          "${pkgs.bash}/bin/bash -c '"
          "  ${pkgs.util-linux}/bin/mount -o remount,bind,ro /nix/store && "
          "  ${pkgs.strace}/bin/strace -f -e trace=mount -o /tmp/strace.log "
          "    ${nix}/bin/nix store info --store local "
          "      --extra-experimental-features nix-command && "
          "  ${pkgs.gnugrep}/bin/grep -q MS_NODEV /tmp/strace.log"
          "'"
      )
    '';
}
