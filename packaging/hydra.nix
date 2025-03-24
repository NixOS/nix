{
  inputs,
  forAllCrossSystems,
  forAllSystems,
  lib,
  linux64BitSystems,
  nixpkgsFor,
  self,
  officialRelease,
}:
let
  inherit (inputs) nixpkgs nixpkgs-regression;

  installScriptFor =
    tarballs:
    nixpkgsFor.x86_64-linux.native.callPackage ./installer {
      inherit tarballs;
    };

  testNixVersions =
    pkgs: daemon:
    pkgs.nixComponents.nix-functional-tests.override {
      pname = "nix-daemon-compat-tests";
      version = "${pkgs.nix.version}-with-daemon-${daemon.version}";

      test-daemon = daemon;
    };

  # Technically we could just return `pkgs.nixComponents`, but for Hydra it's
  # convention to transpose it, and to transpose it efficiently, we need to
  # enumerate them manually, so that we don't evaluate unnecessary package sets.
  forAllPackages = lib.genAttrs [
    "nix-everything"
    "nix-util"
    "nix-util-c"
    "nix-util-test-support"
    "nix-util-tests"
    "nix-store"
    "nix-store-c"
    "nix-store-test-support"
    "nix-store-tests"
    "nix-fetchers"
    "nix-fetchers-tests"
    "nix-expr"
    "nix-expr-c"
    "nix-expr-test-support"
    "nix-expr-tests"
    "nix-flake"
    "nix-flake-tests"
    "nix-main"
    "nix-main-c"
    "nix-cmd"
    "nix-cli"
    "nix-functional-tests"
  ];
in
{
  # Binary package for various platforms.
  build = forAllPackages (
    pkgName: forAllSystems (system: nixpkgsFor.${system}.native.nixComponents.${pkgName})
  );

  shellInputs = removeAttrs (forAllSystems (
    system: self.devShells.${system}.default.inputDerivation
  )) [ "i686-linux" ];

  # Perl bindings for various platforms.
  perlBindings = forAllSystems (system: nixpkgsFor.${system}.native.nixComponents.nix-perl-bindings);

  # Binary tarball for various platforms, containing a Nix store
  # with the closure of 'nix' package, and the second half of
  # the installation script.
  binaryTarball = forAllSystems (
    system: nixpkgsFor.${system}.native.callPackage ./binary-tarball.nix { }
  );

  installerScriptForGHA = forAllSystems (
    system:
    nixpkgsFor.${system}.native.callPackage ./installer {
      tarballs = [ self.hydraJobs.binaryTarball.${system} ];
    }
  );

  # docker image with Nix inside
  dockerImage = lib.genAttrs linux64BitSystems (system: self.packages.${system}.dockerImage);

  # # Line coverage analysis.
  # coverage = nixpkgsFor.x86_64-linux.native.nix.override {
  #   pname = "nix-coverage";
  #   withCoverageChecks = true;
  # };

  # Nix's manual
  manual = nixpkgsFor.x86_64-linux.native.nixComponents.nix-manual;

  # API docs for Nix's unstable internal C++ interfaces.
  internal-api-docs = nixpkgsFor.x86_64-linux.native.nixComponents.nix-internal-api-docs;

  # API docs for Nix's C bindings.
  external-api-docs = nixpkgsFor.x86_64-linux.native.nixComponents.nix-external-api-docs;

  # System tests.
  tests =
    import ../tests/nixos {
      inherit lib nixpkgs nixpkgsFor;
      inherit (self.inputs) nixpkgs-23-11;
    }
    // {

      # Make sure that nix-env still produces the exact same result
      # on a particular version of Nixpkgs.
      evalNixpkgs =
        let
          inherit (nixpkgsFor.x86_64-linux.native) runCommand nix;
        in
        runCommand "eval-nixos" { buildInputs = [ nix ]; } ''
          type -p nix-env
          # Note: we're filtering out nixos-install-tools because https://github.com/NixOS/nixpkgs/pull/153594#issuecomment-1020530593.
          (
            set -x
            time nix-env --store dummy:// -f ${nixpkgs-regression} -qaP --drv-path | sort | grep -v nixos-install-tools > packages
            [[ $(sha1sum < packages | cut -c1-40) = e01b031fc9785a572a38be6bc473957e3b6faad7 ]]
          )
          mkdir $out
        '';

      nixpkgsLibTests = forAllSystems (
        system:
        import (nixpkgs + "/lib/tests/test-with-nix.nix") {
          lib = nixpkgsFor.${system}.native.lib;
          nix = self.packages.${system}.nix-cli;
          pkgs = nixpkgsFor.${system}.native;
        }
      );
    };

  metrics.nixpkgs = import "${nixpkgs-regression}/pkgs/top-level/metrics.nix" {
    pkgs = nixpkgsFor.x86_64-linux.native;
    nixpkgs = nixpkgs-regression;
  };
}
