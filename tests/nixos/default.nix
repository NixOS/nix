{ lib, nixpkgs, nixpkgsFor, self }:

let

  nixos-lib = import (nixpkgs + "/nixos/lib") { };

  noTests = pkg: pkg.overrideAttrs (
            finalAttrs: prevAttrs: {
              doCheck = false;
              doInstallCheck = false;
            });

  # https://nixos.org/manual/nixos/unstable/index.html#sec-calling-nixos-tests
  runNixOSTestFor = system: test:
    (nixos-lib.runTest {
      imports = [
        test
      ];

      hostPkgs = nixpkgsFor.${system}.native;
      defaults = {
        nixpkgs.pkgs = nixpkgsFor.${system}.native;
        nix.checkAllErrors = false;
        # TODO: decide which packaging stage to use. `nix-cli` is efficient, but not the same as the user-facing `everything.nix` package (`default`). Perhaps a good compromise is `everything.nix` + `noTests` defined above?
        nix.package = nixpkgsFor.${system}.native.nixComponents.nix-cli;

        # Evaluate VMs faster
        documentation.enable = false;
      };
      _module.args.nixpkgs = nixpkgs;
      _module.args.system = system;
    })
    // {
      # allow running tests against older nix versions via `nix eval --apply`
      # Example:
      #   nix build "$(nix eval --raw --impure .#hydraJobs.tests.fetch-git --apply 't: (t.forNix "2.19.2").drvPath')^*"
      forNix = nixVersion: runNixOSTestFor system {
        imports = [test];
        defaults.nixpkgs.overlays = [(curr: prev: {
          nix = let
            packages = (builtins.getFlake "nix/${nixVersion}").packages.${system};
          in packages.nix-cli or packages.nix;
        })];
      };
    };

  # Checks that a NixOS configuration does not contain any references to our
  # locally defined Nix version.
  checkOverrideNixVersion = { pkgs, lib, ... }: {
    # pkgs.nix: The new Nix in this repo
    # We disallow it, to make sure we don't accidentally use it.
    system.forbiddenDependenciesRegexes = [
      (lib.strings.escapeRegex "nix-${pkgs.nix.version}")
    ];
  };

  otherNixes.nix_2_3.setNixPackage = { lib, pkgs, ... }: {
    imports = [ checkOverrideNixVersion ];
    nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
  };

  otherNixes.nix_2_13.setNixPackage = { lib, pkgs, ... }: {
    imports = [ checkOverrideNixVersion ];
    nix.package = lib.mkForce (
      self.inputs.nixpkgs-23-11.legacyPackages.${pkgs.stdenv.hostPlatform.system}.nixVersions.nix_2_13.overrideAttrs (o: {
        meta = o.meta // { knownVulnerabilities = []; };
      })
    );
  };

  otherNixes.nix_2_18.setNixPackage = { lib, pkgs, ... }: {
    imports = [ checkOverrideNixVersion ];
    nix.package = lib.mkForce pkgs.nixVersions.nix_2_18;
  };

in

{
  authorization = runNixOSTestFor "x86_64-linux" ./authorization.nix;

  remoteBuilds = runNixOSTestFor "x86_64-linux" ./remote-builds.nix;

  remoteBuildsSshNg = runNixOSTestFor "x86_64-linux" ./remote-builds-ssh-ng.nix;

}
// lib.concatMapAttrs (
  nixVersion: { setNixPackage, ... }:
  {
    "remoteBuilds_remote_${nixVersion}" = runNixOSTestFor "x86_64-linux" {
      name = "remoteBuilds_remote_${nixVersion}";
      imports = [ ./remote-builds.nix ];
      builders.config = { lib, pkgs, ... }: {
        imports = [ setNixPackage ];
      };
    };

    "remoteBuilds_local_${nixVersion}" = runNixOSTestFor "x86_64-linux" {
      name = "remoteBuilds_local_${nixVersion}";
      imports = [ ./remote-builds.nix ];
      nodes.client = { lib, pkgs, ... }: {
        imports = [ setNixPackage ];
      };
    };

    "remoteBuildsSshNg_remote_${nixVersion}" = runNixOSTestFor "x86_64-linux" {
      name = "remoteBuildsSshNg_remote_${nixVersion}";
      imports = [ ./remote-builds-ssh-ng.nix ];
      builders.config = { lib, pkgs, ... }: {
        imports = [ setNixPackage ];
      };
    };

    # FIXME: these tests don't work yet

    # "remoteBuildsSshNg_local_${nixVersion}" = runNixOSTestFor "x86_64-linux" {
    #   name = "remoteBuildsSshNg_local_${nixVersion}";
    #   imports = [ ./remote-builds-ssh-ng.nix ];
    #   nodes.client = { lib, pkgs, ... }: {
    #     imports = [ overridingModule ];
    #   };
    # };
  }
) otherNixes
// {

  nix-copy-closure = runNixOSTestFor "x86_64-linux" ./nix-copy-closure.nix;

  nix-copy = runNixOSTestFor "x86_64-linux" ./nix-copy.nix;

  nix-docker = runNixOSTestFor "x86_64-linux" ./nix-docker.nix;

  nssPreload = runNixOSTestFor "x86_64-linux" ./nss-preload.nix;

  githubFlakes = runNixOSTestFor "x86_64-linux" ./github-flakes.nix;

  gitSubmodules = runNixOSTestFor "x86_64-linux" ./git-submodules.nix;

  sourcehutFlakes = runNixOSTestFor "x86_64-linux" ./sourcehut-flakes.nix;

  tarballFlakes = runNixOSTestFor "x86_64-linux" ./tarball-flakes.nix;

  containers = runNixOSTestFor "x86_64-linux" ./containers/containers.nix;

  setuid = lib.genAttrs
    ["x86_64-linux"]
    (system: runNixOSTestFor system ./setuid.nix);

  fetch-git = runNixOSTestFor "x86_64-linux" ./fetch-git;

  ca-fd-leak = runNixOSTestFor "x86_64-linux" ./ca-fd-leak;

  gzip-content-encoding = runNixOSTestFor "x86_64-linux" ./gzip-content-encoding.nix;

  functional_user = runNixOSTestFor "x86_64-linux" ./functional/as-user.nix;

  functional_trusted = runNixOSTestFor "x86_64-linux" ./functional/as-trusted-user.nix;

  functional_root = runNixOSTestFor "x86_64-linux" ./functional/as-root.nix;

  functional_symlinked-home = runNixOSTestFor "x86_64-linux" ./functional/symlinked-home.nix;

  user-sandboxing = runNixOSTestFor "x86_64-linux" ./user-sandboxing;

  s3-binary-cache-store = runNixOSTestFor "x86_64-linux" ./s3-binary-cache-store.nix;

  fsync = runNixOSTestFor "x86_64-linux" ./fsync.nix;

  cgroups = runNixOSTestFor "x86_64-linux" ./cgroups;

  fetchurl = runNixOSTestFor "x86_64-linux" ./fetchurl.nix;

  chrootStore = runNixOSTestFor "x86_64-linux" ./chroot-store.nix;
}
