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

in

{
  authorization = runNixOSTestFor "x86_64-linux" ./authorization.nix;

  remoteBuilds = runNixOSTestFor "x86_64-linux" ./remote-builds.nix;

  remoteBuildsSshNg = runNixOSTestFor "x86_64-linux" ./remote-builds-ssh-ng.nix;

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
}
