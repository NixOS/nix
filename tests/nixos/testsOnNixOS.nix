{ lib, ... }:

let
  # FIXME (roberth) reference issue
  inputDerivation = pkg: (pkg.overrideAttrs (o: {
    disallowedReferences = [ ];
  })).inputDerivation;

in
{
  nodes.machine = { config, pkgs, ... }: {

    virtualisation.writableStore = true;
    system.extraDependencies = [
      (inputDerivation config.nix.package)
    ];

    nix.package = (builtins.getFlake "git+file:///home/user/h/nix?rev=a1c7cb8fb6c6511daf4884e140aba8ae1788cd84").packages.x86_64-linux.default;
    nix.settings.substituters = lib.mkForce [];

    nixpkgs.overlays = [
      (final: prev: {
        thisNix = prev.nix;
        nix = config.nix.package;
      })
    ];

    environment.systemPackages = let
      run-test-suite = pkgs.writeShellApplication {
        name = "run-test-suite";
        runtimeInputs = [ pkgs.gnumake pkgs.jq pkgs.git ];
        text = ''
          set -x
          cat /proc/sys/fs/file-max
          ulimit -Hn
          ulimit -Sn
          cd ~
          cp -r ${pkgs.thisNix.overrideAttrs (o: {
            name = "nix-configured-source";
            outputs = [ "out" ];
            separateDebugInfo = false;
            disallowedReferences = [ ];
            buildPhase = ":";
            checkPhase = ":";
            installPhase = ''
              cp -r . $out
            '';
            installCheckPhase = ":";
            fixupPhase = ":";
          })} nix
          chmod -R +w nix
          cd nix

          # Tests we don't need
          echo >tests/plugins/local.mk
          sed -i tests/local.mk \
            -e 's^plugins\.sh^^' \
            -e "s^test-deps += tests/plugins/libplugintest.\$(SO_EXT)^^" \
            ;

          export TEST_SYSTEM_NIX=1
          export version=${config.nix.package.version}
          export NIX_REMOTE_=daemon
          export NIX_REMOTE=daemon
          make -j1 tests/eval-store.sh.test installcheck --keep-going
        '';
      };
    in [
      run-test-suite
    ];
  };
}
