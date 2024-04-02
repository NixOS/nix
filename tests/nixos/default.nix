{ lib, nixpkgs, nixpkgsFor }:

let

  nixos-lib = import (nixpkgs + "/nixos/lib") { };

  # https://nixos.org/manual/nixos/unstable/index.html#sec-calling-nixos-tests
  runNixOSTestFor = system: test:
    (nixos-lib.runTest {
      imports = [ test ];
      hostPkgs = nixpkgsFor.${system}.native;
      defaults = {
        nixpkgs.pkgs = nixpkgsFor.${system}.native;
        nix.checkAllErrors = false;
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
          nix = (builtins.getFlake "nix/${nixVersion}").packages.${system}.nix;
        })];
      };
    };

  # Checks that a NixOS configuration does not contain any references to our
  # locally defined Nix version.
  checkOverrideNixVersion = { pkgs, lib, ... }: {
    # pkgs.nix: The new Nix in this repo
    # We disallow it, to make sure we don't accidentally use it.
    system.forbiddenDependenciesRegex = lib.strings.escapeRegex "nix-${pkgs.nix.version}";
  };
in

{
  authorization = runNixOSTestFor "x86_64-linux" ./authorization.nix;

  remoteBuilds = runNixOSTestFor "x86_64-linux" ./remote-builds.nix;

  # Test our Nix as a client against remotes that are older

  remoteBuilds_remote_2_3 = runNixOSTestFor "x86_64-linux" {
    name = "remoteBuilds_remote_2_3";
    imports = [ ./remote-builds.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  };

  remoteBuilds_remote_2_13 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_remote_2_13";
    imports = [ ./remote-builds.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  });

  # TODO: (nixpkgs update) remoteBuilds_remote_2_18 = ...

  # Test our Nix as a builder for clients that are older

  remoteBuilds_local_2_3 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_local_2_3";
    imports = [ ./remote-builds.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  });

  remoteBuilds_local_2_13 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_local_2_13";
    imports = [ ./remote-builds.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_13;
    };
  });

  # TODO: (nixpkgs update) remoteBuilds_local_2_18 = ...

  # End remoteBuilds tests

  remoteBuildsSshNg = runNixOSTestFor "x86_64-linux" ./remote-builds-ssh-ng.nix;

  # Test our Nix as a client against remotes that are older

  remoteBuildsSshNg_remote_2_3 = runNixOSTestFor "x86_64-linux" {
    name = "remoteBuildsSshNg_remote_2_3";
    imports = [ ./remote-builds-ssh-ng.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  };

  remoteBuildsSshNg_remote_2_13 = runNixOSTestFor "x86_64-linux" {
    name = "remoteBuildsSshNg_remote_2_13";
    imports = [ ./remote-builds-ssh-ng.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_13;
    };
  };

  # TODO: (nixpkgs update) remoteBuildsSshNg_remote_2_18 = ...

  # Test our Nix as a builder for clients that are older

  # FIXME: these tests don't work yet
  /*
  remoteBuildsSshNg_local_2_3 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuildsSshNg_local_2_3";
    imports = [ ./remote-builds-ssh-ng.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  });

  remoteBuildsSshNg_local_2_13 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuildsSshNg_local_2_13";
    imports = [ ./remote-builds-ssh-ng.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_13;
    };
  });

  # TODO: (nixpkgs update) remoteBuildsSshNg_local_2_18 = ...
  */

  nix-copy-closure = runNixOSTestFor "x86_64-linux" ./nix-copy-closure.nix;

  nix-copy = runNixOSTestFor "x86_64-linux" ./nix-copy.nix;

  nssPreload = runNixOSTestFor "x86_64-linux" ./nss-preload.nix;

  githubFlakes = runNixOSTestFor "x86_64-linux" ./github-flakes.nix;

  sourcehutFlakes = runNixOSTestFor "x86_64-linux" ./sourcehut-flakes.nix;

  tarballFlakes = runNixOSTestFor "x86_64-linux" ./tarball-flakes.nix;

  containers = runNixOSTestFor "x86_64-linux" ./containers/containers.nix;

  setuid = lib.genAttrs
    ["i686-linux" "x86_64-linux"]
    (system: runNixOSTestFor system ./setuid.nix);

  fetch-git = runNixOSTestFor "x86_64-linux" ./fetch-git;

  ca-fd-leak = runNixOSTestFor "x86_64-linux" ./ca-fd-leak;

  gzip-content-encoding = runNixOSTestFor "x86_64-linux" ./gzip-content-encoding.nix;
}
