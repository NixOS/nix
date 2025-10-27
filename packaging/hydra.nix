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
    pkgs.nixComponents2.nix-functional-tests.override {
      pname = "nix-daemon-compat-tests";
      version = "${pkgs.nix.version}-with-daemon-${daemon.version}";

      test-daemon = daemon;
    };

  # Technically we could just return `pkgs.nixComponents2`, but for Hydra it's
  # convention to transpose it, and to transpose it efficiently, we need to
  # enumerate them manually, so that we don't evaluate unnecessary package sets.
  # See listingIsComplete below.
  forAllPackages = forAllPackages' { };
  forAllPackages' =
    {
      enableBindings ? false,
      enableDocs ? false, # already have separate attrs for these
    }:
    lib.genAttrs (
      [
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
        "nix-fetchers-c"
        "nix-fetchers-tests"
        "nix-expr"
        "nix-expr-c"
        "nix-expr-test-support"
        "nix-expr-tests"
        "nix-flake"
        "nix-flake-c"
        "nix-flake-tests"
        "nix-main"
        "nix-main-c"
        "nix-cmd"
        "nix-cli"
        "nix-functional-tests"
        "nix-json-schema-checks"
        "nix-kaitai-struct-checks"
      ]
      ++ lib.optionals enableBindings [
        "nix-perl-bindings"
      ]
      ++ lib.optionals enableDocs [
        "nix-manual"
        "nix-internal-api-docs"
        "nix-external-api-docs"
      ]
    );
in
rec {
  /**
    An internal check to make sure our package listing is complete.
  */
  listingIsComplete =
    let
      arbitrarySystem = "x86_64-linux";
      listedPkgs = forAllPackages' {
        enableBindings = true;
        enableDocs = true;
      } (_: null);
      actualPkgs = lib.concatMapAttrs (
        k: v: if lib.strings.hasPrefix "nix-" k then { ${k} = null; } else { }
      ) nixpkgsFor.${arbitrarySystem}.native.nixComponents2;
      diff = lib.concatStringsSep "\n" (
        lib.concatLists (
          lib.mapAttrsToList (
            k: _:
            if (listedPkgs ? ${k}) && !(actualPkgs ? ${k}) then
              [ "- ${k}: redundant?" ]
            else if !(listedPkgs ? ${k}) && (actualPkgs ? ${k}) then
              [ "- ${k}: missing?" ]
            else
              [ ]
          ) (listedPkgs // actualPkgs)
        )
      );
    in
    if listedPkgs == actualPkgs then
      { }
    else
      throw ''
        Please update the components list in hydra.nix (or fix this check)
        Differences:
        ${diff}
      '';

  # Binary package for various platforms.
  build = forAllPackages (
    pkgName: forAllSystems (system: nixpkgsFor.${system}.native.nixComponents2.${pkgName})
  );

  shellInputs = removeAttrs (forAllSystems (
    system: self.devShells.${system}.default.inputDerivation
  )) [ "i686-linux" ];

  buildStatic = forAllPackages (
    pkgName:
    lib.genAttrs linux64BitSystems (
      system: nixpkgsFor.${system}.native.pkgsStatic.nixComponents2.${pkgName}
    )
  );

  buildCross = forAllPackages (
    pkgName:
    # Hack to avoid non-evaling package
    (
      if pkgName == "nix-functional-tests" then
        lib.flip builtins.removeAttrs [ "x86_64-w64-mingw32" ]
      else
        lib.id
    )
      (
        forAllCrossSystems (
          crossSystem:
          lib.genAttrs [ "x86_64-linux" ] (
            system: nixpkgsFor.${system}.cross.${crossSystem}.nixComponents2.${pkgName}
          )
        )
      )
  );

  # Builds with sanitizers already have GC disabled, so this buildNoGc can just
  # point to buildWithSanitizers in order to reduce the load on hydra.
  buildNoGc = buildWithSanitizers;

  buildWithSanitizers =
    let
      components = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor.${system}.native;
        in
        pkgs.nixComponents2.overrideScope (
          self: super: {
            # Boost coroutines fail with ASAN on darwin.
            withASan = !pkgs.stdenv.buildPlatform.isDarwin;
            withUBSan = true;
            nix-expr = super.nix-expr.override { enableGC = false; };
            # Unclear how to make Perl bindings work with a dynamically linked ASAN.
            nix-perl-bindings = null;
          }
        )
      );
    in
    forAllPackages (pkgName: forAllSystems (system: components.${system}.${pkgName}));

  buildNoTests = forAllSystems (system: nixpkgsFor.${system}.native.nixComponents2.nix-cli);

  # Toggles some settings for better coverage. Windows needs these
  # library combinations, and Debian build Nix with GNU readline too.
  buildReadlineNoMarkdown =
    let
      components = forAllSystems (
        system:
        nixpkgsFor.${system}.native.nixComponents2.overrideScope (
          self: super: {
            nix-cmd = super.nix-cmd.override {
              enableMarkdown = false;
              readlineFlavor = "readline";
            };
          }
        )
      );
    in
    forAllPackages (pkgName: forAllSystems (system: components.${system}.${pkgName}));

  # Perl bindings for various platforms.
  perlBindings = forAllSystems (system: nixpkgsFor.${system}.native.nixComponents2.nix-perl-bindings);

  # Binary tarball for various platforms, containing a Nix store
  # with the closure of 'nix' package, and the second half of
  # the installation script.
  binaryTarball = forAllSystems (
    system: nixpkgsFor.${system}.native.callPackage ./binary-tarball.nix { }
  );

  binaryTarballCross = lib.genAttrs [ "x86_64-linux" ] (
    system:
    forAllCrossSystems (
      crossSystem: nixpkgsFor.${system}.cross.${crossSystem}.callPackage ./binary-tarball.nix { }
    )
  );

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

  installerScriptForGHA = forAllSystems (
    system:
    nixpkgsFor.${system}.native.callPackage ./installer {
      tarballs = [ self.hydraJobs.binaryTarball.${system} ];
    }
  );

  # docker image with Nix inside
  dockerImage = lib.genAttrs linux64BitSystems (system: self.packages.${system}.dockerImage);

  # # Line coverage analysis.
  coverage =
    (import ./../ci/gha/tests rec {
      withCoverage = true;
      pkgs = nixpkgsFor.x86_64-linux.nativeForStdenv.clangStdenv;
      nixComponents = pkgs.nixComponents2;
      nixFlake = null;
      getStdenv = p: p.clangStdenv;
    }).codeCoverage.coverageReports.overrideAttrs
      {
        name = "nix-coverage"; # For historical consistency
      };

  # Nix's manual
  manual = nixpkgsFor.x86_64-linux.native.nixComponents2.nix-manual;

  # API docs for Nix's unstable internal C++ interfaces.
  internal-api-docs = nixpkgsFor.x86_64-linux.native.nixComponents2.nix-internal-api-docs;

  # API docs for Nix's C bindings.
  external-api-docs = nixpkgsFor.x86_64-linux.native.nixComponents2.nix-external-api-docs;

  # System tests.
  tests =
    import ../tests/nixos {
      inherit lib nixpkgs;
      pkgs = nixpkgsFor.x86_64-linux.native;
      nixComponents = nixpkgsFor.x86_64-linux.native.nixComponents2;
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

  installTests = forAllSystems (
    system:
    let
      pkgs = nixpkgsFor.${system}.native;
    in
    pkgs.runCommand "install-tests" {
      againstSelf = testNixVersions pkgs pkgs.nix;
      againstCurrentLatest =
        # FIXME: temporarily disable this on macOS because of #3605.
        if system == "x86_64-linux" then testNixVersions pkgs pkgs.nixVersions.latest else null;
      # Disabled because the latest stable version doesn't handle
      # `NIX_DAEMON_SOCKET_PATH` which is required for the tests to work
      # againstLatestStable = testNixVersions pkgs pkgs.nixStable;
    } "touch $out"
  );

  installerTests = import ../tests/installer {
    binaryTarballs = self.hydraJobs.binaryTarball;
    inherit nixpkgsFor;
  };
}
