# Test whether we can run a NixOS container inside a Nix build using systemd-nspawn.
{ nixpkgs, system, overlay }:

with import (nixpkgs + "/nixos/lib/testing-python.nix") {
  inherit system;
  extraConfigurations = [ { nixpkgs.overlays = [ overlay ]; } ];
};

makeTest ({
  name = "containers";

  nodes =
    {
      host =
        { config, lib, pkgs, nodes, ... }:
        { virtualisation.writableStore = true;
          virtualisation.diskSize = 2048;
          virtualisation.additionalPaths =
            [ pkgs.stdenv
              (import ./systemd-nspawn.nix { inherit nixpkgs; }).toplevel
            ];
          virtualisation.memorySize = 4096;
          nix.binaryCaches = lib.mkForce [ ];
          nix.extraOptions =
            ''
              extra-experimental-features = nix-command auto-allocate-uids cgroups
              extra-system-features = uid-range
            '';
          nix.nixPath = [ "nixpkgs=${nixpkgs}" ];
        };
    };

  testScript = { nodes }: ''
    start_all()

    host.succeed("nix --version >&2")

    # Test that 'id' gives the expected result in various configurations.

    # Existing UIDs, sandbox.
    host.succeed("nix build --no-auto-allocate-uids --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-1")
    host.succeed("[[ $(cat ./result) = 'uid=1000(nixbld) gid=100(nixbld) groups=100(nixbld)' ]]")

    # Existing UIDs, no sandbox.
    host.succeed("nix build --no-auto-allocate-uids --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-2")
    host.succeed("[[ $(cat ./result) = 'uid=30001(nixbld1) gid=30000(nixbld) groups=30000(nixbld)' ]]")

    # Auto-allocated UIDs, sandbox.
    host.succeed("nix build --auto-allocate-uids --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-3")
    host.succeed("[[ $(cat ./result) = 'uid=1000(nixbld) gid=100(nixbld) groups=100(nixbld)' ]]")

    # Auto-allocated UIDs, no sandbox.
    host.succeed("nix build --auto-allocate-uids --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-4")
    host.succeed("[[ $(cat ./result) = 'uid=872415232 gid=30000(nixbld) groups=30000(nixbld)' ]]")

    # Auto-allocated UIDs, UID range, sandbox.
    host.succeed("nix build --auto-allocate-uids --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-5 --arg uidRange true")
    host.succeed("[[ $(cat ./result) = 'uid=0(root) gid=0(root) groups=0(root)' ]]")

    # Auto-allocated UIDs, UID range, no sandbox.
    host.fail("nix build --auto-allocate-uids --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-6 --arg uidRange true")

    # Run systemd-nspawn in a Nix build.
    host.succeed("nix build --auto-allocate-uids --sandbox -L --offline --impure --file ${./systemd-nspawn.nix} --argstr nixpkgs ${nixpkgs}")
    host.succeed("[[ $(cat ./result/msg) = 'Hello World' ]]")
  '';

})
