{ nixpkgs, ... }:

{
  name = "cgroups";

  nodes = {
    host =
      { config, pkgs, ... }:
      {
        virtualisation.additionalPaths = [ pkgs.stdenvNoCC ];
        nix.extraOptions = ''
          extra-experimental-features = nix-command auto-allocate-uids cgroups
          extra-system-features = uid-range
        '';
        nix.settings.use-cgroups = true;
        nix.nixPath = [ "nixpkgs=${nixpkgs}" ];
      };
  };

  testScript =
    { nodes }:
    ''
      start_all()

      host.wait_for_unit("multi-user.target")

      # Start build in background
      host.execute("NIX_REMOTE=daemon nix build --auto-allocate-uids --file ${./hang.nix} >&2 &")
      service = "/sys/fs/cgroup/system.slice/nix-daemon.service"

      # Wait for cgroups to be created
      host.succeed(f"until [ -e {service}/nix-daemon ]; do sleep 1; done", timeout=30)
      host.succeed(f"until [ -e {service}/nix-build-uid-* ]; do sleep 1; done", timeout=30)

      # Check that there aren't processes where there shouldn't be, and that there are where there should be
      host.succeed(f'[ -z "$(cat {service}/cgroup.procs)" ]')
      host.succeed(f'[ -n "$(cat {service}/nix-daemon/cgroup.procs)" ]')
      host.succeed(f'[ -n "$(cat {service}/nix-build-uid-*/cgroup.procs)" ]')
    '';

}
