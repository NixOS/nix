{ lib, nixComponents, ... }:

let
  # FIXME (roberth) reference issue
  inputDerivation =
    pkg:
    (pkg.overrideAttrs (o: {
      disallowedReferences = [ ];
    })).inputDerivation;

in
{
  # We rarely change the script in a way that benefits from type checking, so
  # we skip it to save time.
  skipTypeCheck = true;

  nodes.machine =
    { config, pkgs, ... }:
    {

      virtualisation.writableStore = true;
      system.extraDependencies = [
        (inputDerivation config.nix.package)
      ];

      nix.settings.substituters = lib.mkForce [ ];

      environment.systemPackages =
        let
          run-test-suite = pkgs.writeShellApplication {
            name = "run-test-suite";
            runtimeInputs = [
              pkgs.meson
              pkgs.ninja
              pkgs.jq
              pkgs.git

              # Want to avoid `/run/current-system/sw/bin/bash` because we
              # want a store path. Likewise for coreutils.
              pkgs.bash
              pkgs.coreutils
            ];
            text = ''
              set -x

              cat /proc/sys/fs/file-max
              ulimit -Hn
              ulimit -Sn

              cd ~

              cp -r ${nixComponents.nix-functional-tests.src} nix
              chmod -R +w nix

              chmod u+w nix/.version
              echo ${nixComponents.version} > nix/.version

              export isTestOnNixOS=1

              export NIX_REMOTE_=daemon
              export NIX_REMOTE=daemon

              export NIX_STORE=${builtins.storeDir}

              meson setup nix/tests/functional build
              cd build
              meson test -j1 --print-errorlogs
            '';
          };
        in
        [
          run-test-suite
          pkgs.git
        ];
    };
}
