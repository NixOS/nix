{ lib, ... }:

let
  # FIXME (roberth) reference issue
  inputDerivation = pkg: (pkg.overrideAttrs (o: {
    disallowedReferences = [ ];
  })).inputDerivation;

in
{
  # We rarely change the script in a way that benefits from type checking, so
  # we skip it to save time.
  skipTypeCheck = true;

  nodes.machine = { config, pkgs, ... }: {

    virtualisation.writableStore = true;
    system.extraDependencies = [
      (inputDerivation config.nix.package)
    ];

    nix.settings.substituters = lib.mkForce [];

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
          cp -r ${pkgs.nix.overrideAttrs (o: {
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
            doInstallCheck = true;
          })} nix
          chmod -R +w nix
          cd nix

          # Tests we don't need
          echo >tests/functional/plugins/local.mk
          sed -i tests/functional/local.mk \
            -e 's!nix_tests += plugins\.sh!!' \
            -e 's!nix_tests += test-libstoreconsumer\.sh!!' \
            ;

          export isTestOnNixOS=1
          export version=${config.nix.package.version}
          export NIX_REMOTE_=daemon
          export NIX_REMOTE=daemon
          export NIX_STORE=${builtins.storeDir}
          make -j1 installcheck --keep-going
        '';
      };
    in [
      run-test-suite
      pkgs.git
    ];
  };
}
