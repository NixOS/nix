{ inputs
, binaryTarball
, forAllCrossSystems
, forAllSystems
, lib
, linux64BitSystems
, nixpkgsFor
, self
}:
let
  inherit (inputs) nixpkgs nixpkgs-regression;
  inherit (lib) fileset;

  installScriptFor = tarballs:
    nixpkgsFor.x86_64-linux.native.callPackage ../scripts/installer.nix {
      inherit tarballs;
    };

  testNixVersions = pkgs: client: daemon:
    pkgs.callPackage ../package.nix {
      pname =
        "nix-tests"
        + lib.optionalString
          (lib.versionAtLeast daemon.version "2.4pre20211005" &&
           lib.versionAtLeast client.version "2.4pre20211005")
          "-${client.version}-against-${daemon.version}";

      inherit fileset;

      test-client = client;
      test-daemon = daemon;

      doBuild = false;
    };
in
{
  # Binary package for various platforms.
  build = forAllSystems (system: self.packages.${system}.nix);

  shellInputs = forAllSystems (system: self.devShells.${system}.default.inputDerivation);

  buildStatic = lib.genAttrs linux64BitSystems (system: self.packages.${system}.nix-static);

  buildCross = forAllCrossSystems (crossSystem:
    lib.genAttrs [ "x86_64-linux" ] (system: self.packages.${system}."nix-${crossSystem}"));

  buildNoGc = forAllSystems (system:
    self.packages.${system}.nix.override { enableGC = false; }
  );

  buildNoTests = forAllSystems (system:
    self.packages.${system}.nix.override {
      doCheck = false;
      doInstallCheck = false;
      installUnitTests = false;
    }
  );

  # Toggles some settings for better coverage. Windows needs these
  # library combinations, and Debian build Nix with GNU readline too.
  buildReadlineNoMarkdown = forAllSystems (system:
    self.packages.${system}.nix.override {
      enableMarkdown = false;
      readlineFlavor = "readline";
    }
  );

  # Perl bindings for various platforms.
  perlBindings = forAllSystems (system: nixpkgsFor.${system}.native.nix.perl-bindings);

  # Binary tarball for various platforms, containing a Nix store
  # with the closure of 'nix' package, and the second half of
  # the installation script.
  binaryTarball = forAllSystems (system: binaryTarball nixpkgsFor.${system}.native.nix nixpkgsFor.${system}.native);

  binaryTarballCross = lib.genAttrs [ "x86_64-linux" ] (system:
    forAllCrossSystems (crossSystem:
      binaryTarball
        self.packages.${system}."nix-${crossSystem}"
        nixpkgsFor.${system}.cross.${crossSystem}));

  # The first half of the installation script. This is uploaded
  # to https://nixos.org/nix/install. It downloads the binary
  # tarball for the user's system and calls the second half of the
  # installation script.
  installerScript = installScriptFor [
    # Native
    self.hydraJobs.binaryTarball."x86_64-linux"
    self.hydraJobs.binaryTarball."i686-linux"
    self.hydraJobs.binaryTarball."aarch64-linux"
    self.hydraJobs.binaryTarball."x86_64-darwin"
    self.hydraJobs.binaryTarball."aarch64-darwin"
    # Cross
    self.hydraJobs.binaryTarballCross."x86_64-linux"."armv6l-unknown-linux-gnueabihf"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."armv7l-unknown-linux-gnueabihf"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."riscv64-unknown-linux-gnu"
  ];
  installerScriptForGHA = installScriptFor [
    # Native
    self.hydraJobs.binaryTarball."x86_64-linux"
    self.hydraJobs.binaryTarball."aarch64-darwin"
    # Cross
    self.hydraJobs.binaryTarballCross."x86_64-linux"."armv6l-unknown-linux-gnueabihf"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."armv7l-unknown-linux-gnueabihf"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."riscv64-unknown-linux-gnu"
  ];

  # docker image with Nix inside
  dockerImage = lib.genAttrs linux64BitSystems (system: self.packages.${system}.dockerImage);

  # Line coverage analysis.
  coverage = nixpkgsFor.x86_64-linux.native.nix.override {
    pname = "nix-coverage";
    withCoverageChecks = true;
  };

  # API docs for Nix's unstable internal C++ interfaces.
  internal-api-docs = nixpkgsFor.x86_64-linux.native.callPackage ../package.nix {
    inherit fileset;
    doBuild = false;
    enableInternalAPIDocs = true;
  };

  # API docs for Nix's C bindings.
  external-api-docs = nixpkgsFor.x86_64-linux.native.callPackage ../package.nix {
    inherit fileset;
    doBuild = false;
    enableExternalAPIDocs = true;
  };

  # System tests.
  tests = import ../tests/nixos { inherit lib nixpkgs nixpkgsFor; } // {

    # Make sure that nix-env still produces the exact same result
    # on a particular version of Nixpkgs.
    evalNixpkgs =
      let
        inherit (nixpkgsFor.x86_64-linux.native) runCommand nix;
      in
      runCommand "eval-nixos" { buildInputs = [ nix ]; }
        ''
          type -p nix-env
          # Note: we're filtering out nixos-install-tools because https://github.com/NixOS/nixpkgs/pull/153594#issuecomment-1020530593.
          (
            set -x
            time nix-env --store dummy:// -f ${nixpkgs-regression} -qaP --drv-path | sort | grep -v nixos-install-tools > packages
            [[ $(sha1sum < packages | cut -c1-40) = e01b031fc9785a572a38be6bc473957e3b6faad7 ]]
          )
          mkdir $out
        '';

    nixpkgsLibTests =
      forAllSystems (system:
        import (nixpkgs + "/lib/tests/release.nix")
          {
            pkgs = nixpkgsFor.${system}.native;
            nixVersions = [ self.packages.${system}.nix ];
          }
      );
  };

  metrics.nixpkgs = import "${nixpkgs-regression}/pkgs/top-level/metrics.nix" {
    pkgs = nixpkgsFor.x86_64-linux.native;
    nixpkgs = nixpkgs-regression;
  };

  installTests = forAllSystems (system:
    let pkgs = nixpkgsFor.${system}.native; in
    pkgs.runCommand "install-tests"
      {
        againstSelf = testNixVersions pkgs pkgs.nix pkgs.pkgs.nix;
        againstCurrentUnstable =
          # FIXME: temporarily disable this on macOS because of #3605.
          if system == "x86_64-linux"
          then testNixVersions pkgs pkgs.nix pkgs.nixUnstable
          else null;
        # Disabled because the latest stable version doesn't handle
        # `NIX_DAEMON_SOCKET_PATH` which is required for the tests to work
        # againstLatestStable = testNixVersions pkgs pkgs.nix pkgs.nixStable;
      } "touch $out");

  installerTests = import ../tests/installer {
    binaryTarballs = self.hydraJobs.binaryTarball;
    inherit nixpkgsFor;
  };
}
