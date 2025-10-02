{
  lib,
  pkgs,
  nixComponents,
  nixpkgs,
  nixpkgs-23-11,
}:

let

  nixos-lib = import (nixpkgs + "/nixos/lib") { };

  noTests =
    pkg:
    pkg.overrideAttrs (
      finalAttrs: prevAttrs: {
        doCheck = false;
        doInstallCheck = false;
      }
    );

  # https://nixos.org/manual/nixos/unstable/index.html#sec-calling-nixos-tests
  runNixOSTest =
    test:
    (nixos-lib.runTest {
      imports = [
        test
      ];

      hostPkgs = pkgs;
      defaults = {
        nixpkgs.pkgs = pkgs;
        nix.checkAllErrors = false;
        # TODO: decide which packaging stage to use. `nix-cli` is efficient, but not the same as the user-facing `everything.nix` package (`default`). Perhaps a good compromise is `everything.nix` + `noTests` defined above?
        nix.package = nixComponents.nix-cli;

        # Evaluate VMs faster
        documentation.enable = false;
        # this links against nix and might break with our git version.
        system.tools.nixos-option.enable = false;
      };
      _module.args.nixComponents = nixComponents;
      _module.args.nixpkgs = nixpkgs;
      _module.args.system = pkgs.system;
    })
    // {
      # allow running tests against older nix versions via `nix eval --apply`
      # Example:
      #   nix build "$(nix eval --raw --impure .#hydraJobs.tests.fetch-git --apply 't: (t.forNix "2.19.2").drvPath')^*"
      forNix =
        nixVersion:
        runNixOSTest {
          imports = [ test ];
          defaults.nixpkgs.overlays = [
            (curr: prev: {
              nix =
                let
                  packages = (builtins.getFlake "nix/${nixVersion}").packages.${pkgs.system};
                in
                packages.nix-cli or packages.nix;
            })
          ];
        };
    };

  # Checks that a NixOS configuration does not contain any references to our
  # locally defined Nix version.
  checkOverrideNixVersion =
    { pkgs, lib, ... }:
    {
      # pkgs.nix: The new Nix in this repo
      # We disallow it, to make sure we don't accidentally use it.
      system.forbiddenDependenciesRegexes = [
        (lib.strings.escapeRegex "nix-${pkgs.nix.version}")
      ];
    };

  otherNixes.nix_2_3.setNixPackage =
    { lib, pkgs, ... }:
    {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce (
        pkgs.nixVersions.nix_2_3.overrideAttrs (o: {
          meta = o.meta // {
            # This version shouldn't be used by end-users, but we run tests against
            # it to ensure we don't break protocol compatibility.
            knownVulnerabilities = [ ];
          };
        })
      );
    };

  otherNixes.nix_2_13.setNixPackage =
    { lib, pkgs, ... }:
    {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce (
        nixpkgs-23-11.legacyPackages.${pkgs.stdenv.hostPlatform.system}.nixVersions.nix_2_13.overrideAttrs
          (o: {
            meta = o.meta // {
              # This version shouldn't be used by end-users, but we run tests against
              # it to ensure we don't break protocol compatibility.
              knownVulnerabilities = [ ];
            };
          })
      );
    };

in

{
  authorization = runNixOSTest ./authorization.nix;

  remoteBuilds = runNixOSTest ./remote-builds.nix;

  remoteBuildsSshNg = runNixOSTest ./remote-builds-ssh-ng.nix;

}
// lib.concatMapAttrs (
  nixVersion:
  { setNixPackage, ... }:
  {
    "remoteBuilds_remote_${nixVersion}" = runNixOSTest {
      name = "remoteBuilds_remote_${nixVersion}";
      imports = [ ./remote-builds.nix ];
      builders.config =
        { lib, pkgs, ... }:
        {
          imports = [ setNixPackage ];
        };
    };

    "remoteBuilds_local_${nixVersion}" = runNixOSTest {
      name = "remoteBuilds_local_${nixVersion}";
      imports = [ ./remote-builds.nix ];
      nodes.client =
        { lib, pkgs, ... }:
        {
          imports = [ setNixPackage ];
        };
    };

    "remoteBuildsSshNg_remote_${nixVersion}" = runNixOSTest {
      name = "remoteBuildsSshNg_remote_${nixVersion}";
      imports = [ ./remote-builds-ssh-ng.nix ];
      builders.config =
        { lib, pkgs, ... }:
        {
          imports = [ setNixPackage ];
        };
    };

    # FIXME: these tests don't work yet

    # "remoteBuildsSshNg_local_${nixVersion}" = runNixOSTest {
    #   name = "remoteBuildsSshNg_local_${nixVersion}";
    #   imports = [ ./remote-builds-ssh-ng.nix ];
    #   nodes.client = { lib, pkgs, ... }: {
    #     imports = [ overridingModule ];
    #   };
    # };
  }
) otherNixes
// {

  nix-copy-closure = runNixOSTest ./nix-copy-closure.nix;

  nix-copy = runNixOSTest ./nix-copy.nix;

  nix-docker = runNixOSTest ./nix-docker.nix;

  nssPreload = runNixOSTest ./nss-preload.nix;

  githubFlakes = runNixOSTest ./github-flakes.nix;

  gitSubmodules = runNixOSTest ./git-submodules.nix;

  sourcehutFlakes = runNixOSTest ./sourcehut-flakes.nix;

  tarballFlakes = runNixOSTest ./tarball-flakes.nix;

  containers = runNixOSTest ./containers/containers.nix;

  setuid = runNixOSTest ./setuid.nix;

  fetch-git = runNixOSTest ./fetch-git;

  ca-fd-leak = runNixOSTest ./ca-fd-leak;

  content-encoding = runNixOSTest ./content-encoding.nix;

  functional_user = runNixOSTest ./functional/as-user.nix;

  functional_trusted = runNixOSTest ./functional/as-trusted-user.nix;

  functional_root = runNixOSTest ./functional/as-root.nix;

  functional_symlinked-home = runNixOSTest ./functional/symlinked-home.nix;

  user-sandboxing = runNixOSTest ./user-sandboxing;

  s3-binary-cache-store = runNixOSTest ./s3-binary-cache-store.nix;

  fsync = runNixOSTest ./fsync.nix;

  cgroups = runNixOSTest ./cgroups;

  fetchurl = runNixOSTest ./fetchurl.nix;

  fetchersSubstitute = runNixOSTest ./fetchers-substitute.nix;

  chrootStore = runNixOSTest ./chroot-store.nix;
}
