{ inputs
, forAllCrossSystems
, forAllSystems
, lib
, linux64BitSystems
, nixpkgsFor
, self
, officialRelease
}:
let
  inherit (inputs) nixpkgs nixpkgs-regression;

  installScriptFor = tarballs:
    nixpkgsFor.x86_64-linux.native.stdenvPackages.callPackage ./installer {
      inherit tarballs;
    };

  testNixVersions = pkgs: daemon:
    pkgs.nixComponents.nix-functional-tests.override {
      pname =
        "nix-tests"
        + lib.optionalString
          (lib.versionAtLeast daemon.version "2.4pre20211005" &&
           lib.versionAtLeast pkgs.nix.version "2.4pre20211005")
          "-${pkgs.nix.version}-against-${daemon.version}";

      test-daemon = daemon;
    };

  # Technically we could just return `pkgs.nixComponents`, but for Hydra it's
  # convention to transpose it, and to transpose it efficiently, we need to
  # enumerate them manually, so that we don't evaluate unnecessary package sets.
  forAllPackages = lib.genAttrs [
    "nix"
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
    "nix-ng"
  ];
in
{
  # Binary package for various platforms.
  build = forAllPackages (pkgName:
    forAllSystems (system: nixpkgsFor.${system}.native.stdenvPackages.nixComponents.${pkgName}));

  shellInputs = forAllSystems (system: self.devShells.${system}.default.inputDerivation);

  buildStatic = forAllPackages (pkgName:
    lib.genAttrs linux64BitSystems (system: nixpkgsFor.${system}.native.stdenvPackages.pkgsStatic.nixComponents.${pkgName}));

  buildCross = forAllPackages (pkgName:
    # Hack to avoid non-evaling package
    (if pkgName == "nix-functional-tests" then lib.flip builtins.removeAttrs ["x86_64-w64-mingw32"] else lib.id)
    (forAllCrossSystems (crossSystem:
      lib.genAttrs [ "x86_64-linux" ] (system: nixpkgsFor.${system}.cross.${crossSystem}.stdenvPackages.nixComponents.${pkgName}))));

  buildNoGc = let
    components = forAllSystems (system:
      nixpkgsFor.${system}.native.stdenvPackages.nixComponents.overrideScope (self: super: {
        nix-expr = super.nix-expr.override { enableGC = false; };
      })
    );
  in forAllPackages (pkgName: forAllSystems (system: components.${system}.${pkgName}));

  buildNoTests = forAllSystems (system: nixpkgsFor.${system}.native.stdenvPackages.nixComponents.nix-cli);

  # Toggles some settings for better coverage. Windows needs these
  # library combinations, and Debian build Nix with GNU readline too.
  buildReadlineNoMarkdown = let
    components = forAllSystems (system:
      nixpkgsFor.${system}.native.stdenvPackages.nixComponents.overrideScope (self: super: {
        nix-cmd = super.nix-cmd.override {
          enableMarkdown = false;
          readlineFlavor = "readline";
        };
      })
    );
  in forAllPackages (pkgName: forAllSystems (system: components.${system}.${pkgName}));

  # Perl bindings for various platforms.
  perlBindings = forAllSystems (system: nixpkgsFor.${system}.native.stdenvPackages.nixComponents.nix-perl-bindings);

  # Binary tarball for various platforms, containing a Nix store
  # with the closure of 'nix' package, and the second half of
  # the installation script.
  binaryTarball = forAllSystems (system:
    nixpkgsFor.${system}.native.stdenvPackages.callPackage ./binary-tarball.nix {});

  binaryTarballCross = lib.genAttrs [ "x86_64-linux" ] (system:
    forAllCrossSystems (crossSystem:
      nixpkgsFor.${system}.cross.${crossSystem}.stdenvPackages.callPackage ./binary-tarball.nix {}));

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
  coverage = nixpkgsFor.x86_64-linux.native.stdenvPackages.nix.override {
    pname = "nix-coverage";
    withCoverageChecks = true;
  };

  # Nix's manual
  manual = nixpkgsFor.x86_64-linux.native.stdenvPackages.nixComponents.nix-manual;

  # API docs for Nix's unstable internal C++ interfaces.
  internal-api-docs = nixpkgsFor.x86_64-linux.native.stdenvPackages.nixComponents.nix-internal-api-docs;

  # API docs for Nix's C bindings.
  external-api-docs = nixpkgsFor.x86_64-linux.native.stdenvPackages.nixComponents.nix-external-api-docs;

  # System tests.
  tests = import ../tests/nixos {
    inherit lib nixpkgs nixpkgsFor;
    inherit (self.inputs) nixpkgs-23-11;
  } // {

    # Make sure that nix-env still produces the exact same result
    # on a particular version of Nixpkgs.
    evalNixpkgs =
      let
        inherit (nixpkgsFor.x86_64-linux.native.stdenvPackages) runCommand nix;
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
        import (nixpkgs + "/lib/tests/test-with-nix.nix")
          {
            lib = nixpkgsFor.${system}.native.stdenvPackages.lib;
            nix = self.packages.${system}.nix;
            pkgs = nixpkgsFor.${system}.native.stdenvPackages;
          }
      );
  };

  metrics.nixpkgs = import "${nixpkgs-regression}/pkgs/top-level/metrics.nix" {
    pkgs = nixpkgsFor.x86_64-linux.native.stdenvPackages;
    nixpkgs = nixpkgs-regression;
  };

  installTests = forAllSystems (system:
    let pkgs = nixpkgsFor.${system}.native.stdenvPackages; in
    pkgs.runCommand "install-tests"
      {
        againstSelf = testNixVersions pkgs pkgs.nix;
        againstCurrentLatest =
          # FIXME: temporarily disable this on macOS because of #3605.
          if system == "x86_64-linux"
          then testNixVersions pkgs pkgs.nixVersions.latest
          else null;
        # Disabled because the latest stable version doesn't handle
        # `NIX_DAEMON_SOCKET_PATH` which is required for the tests to work
        # againstLatestStable = testNixVersions pkgs pkgs.nixStable;
      } "touch $out");

  installerTests = import ../tests/installer {
    binaryTarballs = self.hydraJobs.binaryTarball;
    inherit nixpkgsFor;
  };
}
