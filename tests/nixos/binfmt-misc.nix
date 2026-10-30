{
  lib,
  config,
  nixpkgs,
  pkgs,
  nixComponents,
  ...
}:

let
  inherit (pkgs.stdenv.hostPlatform) system;

  # Pick two other systems to test on
  alt1 = if system == "x86_64-linux" then "aarch64-multiplatform" else "gnu64";
  alt2 = if system == "riscv64-linux" then "aarch64-multiplatform" else "riscv64";

  altPkgs1 = pkgs.pkgsCross.${alt1};
  altPkgs2 = pkgs.pkgsCross.${alt2};
  altSystem1 = altPkgs1.stdenv.hostPlatform.system;
  altSystem2 = altPkgs2.stdenv.hostPlatform.system;

  emu2 = altPkgs2.stdenv.hostPlatform.emulator pkgs.pkgsStatic;

  pkgsFor = {
    "${system}" = pkgs;
    "${altSystem1}" = altPkgs1;
    "${altSystem2}" = altPkgs2;
  };
in
{
  options.unprivDaemon = lib.mkOption {
    type = lib.types.bool;
    default = false;
    description = "Use an unprivileged Nix daemon";
  };

  options.oldKernel = lib.mkOption {
    type = lib.types.bool;
    default = false;
    description = "Use an unprivileged Nix daemon";
  };

  config = {
    name = "binfmt-misc";

    nodes.machine = lib.mkMerge [
      {
        boot.binfmt.emulatedSystems = [ altSystem1 ];
        nix.settings = {
          experimental-features = [ "nix-command" ];
          extra-platforms = [ altSystem2 ];
          trusted-users = [ "alice" ];
          substituters = lib.mkForce [ ]; # Make nix-build slightly faster
        };
        system.extraDependencies = [
          pkgs.dash
          altPkgs1.dash
          altPkgs2.dash
          emu2
        ];
      }
      (lib.mkIf config.unprivDaemon {
        users.groups.nix-daemon = { };
        users.users.nix-daemon = {
          isSystemUser = true;
          group = "nix-daemon";
        };
        nix = {
          # We have to use nix-everything for nswrapper, nix-cli doesn't have it.
          package = lib.mkForce nixComponents.nix-everything;
          daemonUser = "nix-daemon";
          daemonGroup = "nix-daemon";
          settings.experimental-features = [
            "local-overlay-store"
            "auto-allocate-uids"
          ];
        };
      })
      (lib.mkIf config.oldKernel {
        boot.kernelPackages = pkgs.linuxPackages_6_6;
      })
    ];

    testScript =
      { nodes }:
      let
        binfmtMagics = import ./binfmt-magics.nix;

        magics2 = binfmtMagics.${altSystem2};
        binfmt2 = pkgs.writeText "binfmt-${altSystem2}" ''
          # This is a comment
          ; This is another comment
          :${altSystem2}:M::${magics2.magicOrExtension}:${magics2.mask or ""}:${emu2}:POF
        '';

        testFor =
          {
            name,
            success,
            system,
            nixArgs ? [ ],
          }:
          ''
            machine.${if success then "succeed" else "fail"}(r"""
              nix-build -v ${lib.escapeShellArgs nixArgs} -E '
                builtins.derivation {
                  name = "${name}-${system}";
                  system = "${system}";
                  builder = builtins.storePath ${lib.getExe pkgsFor.${system}.dash};
                  args = [ "-c" "echo okay > $out" ];
                }
              '
            """.strip())
          '';
      in
      ''
        machine.succeed("nix copy --no-check-sigs --to ./unpriv --stdin < /run/current-system/extra-dependencies")
      ''
      + lib.concatMapStringsSep "\n" testFor [
        {
          name = "native";
          success = true;
          inherit system;
        }
        {
          name = "native-no-binfmt-misc";
          success = !config.oldKernel;
          inherit system;
          nixArgs = [
            "--binfmt-misc"
            "${system}="
          ];
        }
        {
          name = "global";
          success = true;
          system = altSystem1;
        }
        {
          name = "global";
          success = false;
          system = altSystem2;
        }
        {
          name = "namespaced";
          success = !config.oldKernel;
          system = altSystem2;
          nixArgs = [
            "--binfmt-misc"
            "${altSystem2}=${binfmt2}"
          ];
        }
        {
          name = "namespaced-unsandboxed";
          success = false;
          system = altSystem2;
          nixArgs = [
            "--no-sandbox"
            "--binfmt-misc"
            "${altSystem2}=${binfmt2}"
          ];
        }
        {
          name = "namespaced";
          success = false;
          system = altSystem1;
          nixArgs = [
            "--binfmt-misc"
            "${altSystem1}="
          ];
        }
        {
          name = "unpriv-namespaced";
          success = !config.oldKernel;
          system = altSystem2;
          nixArgs = [
            "--store"
            "./unpriv"
            "--build-dir"
            "/home/alice/build"
            "--binfmt-misc"
            "${altSystem2}=${binfmt2}"
          ];
        }
        {
          name = "unpriv-namespaced";
          success = false;
          system = altSystem1;
          nixArgs = [
            "--store"
            "./unpriv"
            "--build-dir"
            "/home/alice/build"
            "--binfmt-misc"
            "${altSystem1}="
          ];
        }
      ];
  };
}
