# This installs an older Nix into a nix-env profile, and then runs `nix upgrade-nix`
# pointing at a local fallback-paths file containing the locally built nix.
#
# This is based on nixpkgs' nixosTests.nix-upgrade test.
# See https://github.com/NixOS/nixpkgs/blob/e3469a82fbd496d9c8e6192bbaf7cf056c6449ff/nixos/tests/nix/upgrade.nix.

{
  config,
  lib,
  nixComponents,
  system,
  ...
}:
let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  newNix = nixComponents.nix-cli;
  oldNix = config.upgrade-nix.oldNix;

  fallback-paths = pkgs.writeTextDir "fallback-paths.nix" ''
    {
      ${system} = "${newNix}";
    }
  '';
in
{
  options.upgrade-nix.oldNix = lib.mkOption {
    type = lib.types.package;
    default = newNix;
    description = "The Nix package to install before upgrading.";
  };

  config = {
    name = "upgrade-nix";

    nodes.machine =
      { lib, ... }:
      {
        virtualisation.writableStore = true;
        nix.settings.substituters = lib.mkForce [ ];
        nix.settings.hashed-mirrors = null;
        nix.settings.connect-timeout = 1;
        nix.extraOptions = "experimental-features = nix-command";
        environment.localBinInPath = true;
        system.extraDependencies = [
          fallback-paths
          newNix
          oldNix
        ];
        users.users.alice = {
          isNormalUser = true;
          packages = [ newNix ];
        };
      };

    testScript = /* py */ ''
      machine.start()
      machine.wait_for_unit("multi-user.target")

      with subtest("nix-current"):
          # Create a profile to pretend we are on non-NixOS

          print(machine.succeed("nix --version"))
          machine.succeed("nix-env -i ${oldNix} -p /root/.local")
          result = machine.succeed("/root/.local/bin/nix --version")
          print(f"installed: {result}")

      with subtest("nix-upgrade"):
          machine.succeed(
              "nix upgrade-nix"
              " --nix-store-paths-url file://${fallback-paths}/fallback-paths.nix"
              " --profile /root/.local"
          )
          result = machine.succeed("/root/.local/bin/nix --version")
          print(f"after upgrade: {result}")
          assert "${newNix.version}" in result, \
              f"expected ${newNix.version} in: {result}"

      with subtest("nix-build-with-mismatched-daemon"):
          # The daemon is still running oldNix; verify the new client works.
          machine.succeed(
              "runuser -u alice -- nix build"
              " --expr 'derivation { name = \"test\"; system = \"${system}\";"
              " builder = \"/bin/sh\"; args = [\"-c\" \"echo test > $out\"]; }'"
              " --print-out-paths"
          )

      with subtest("nix-upgrade-auto-detect"):
          # Without passing in --profile, getProfileDir auto-detects the profile
          # by finding nix-env in PATH and resolving the symlink chain back
          # to the store.
          machine.succeed("nix-env -i ${oldNix} -p /root/.local")
          machine.succeed(
              "PATH=/root/.local/bin:$PATH"
              " ${newNix}/bin/nix upgrade-nix"
              " --nix-store-paths-url file://${fallback-paths}/fallback-paths.nix"
          )
          result = machine.succeed("/root/.local/bin/nix --version")
          print(f"after auto-detect upgrade: {result}")
          assert "${newNix.version}" in result, \
              f"expected ${newNix.version} in: {result}"
    '';
  };
}
