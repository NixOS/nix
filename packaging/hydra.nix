{
  inputs,
  forAllCrossSystems,
  forAllSystems,
  lib,
  linux64BitSystems,
  nixpkgsFor,
  nixComponentsFor,
  self,
}:
let
  inherit (inputs) nixpkgs nixpkgs-regression;

  installScriptFor =
    tarballs:
    nixpkgsFor.x86_64-linux.native.callPackage ./installer {
      inherit tarballs;
      # Platform doesn't matter, we only need to fish out the fineVersion.
      version = nixComponentsFor.x86_64-linux.native.nix-cli.version;
    };

  testNixVersions =
    components: daemon:
    components.nix-functional-tests.override {
      pname = "nix-daemon-compat-tests";
      version = "${components.nix-cli.version}-with-daemon-${daemon.version}";

      test-daemon = daemon;
    };

  # Technically we could just return `nixComponents`, but for Hydra it's
  # convention to transpose it, and to transpose it efficiently, we need to
  # enumerate them manually, so that we don't evaluate unnecessary package sets.
  # See listingIsComplete below.
  forAllPackages = forAllPackages' { };
  forAllPackages' =
    {
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
        "nix-nswrapper"
        "nix-main"
        "nix-main-c"
        "nix-cmd"
        "nix-cli"
        "nix-functional-tests"
        "nix-json-schema-checks"
        "nix-clang-tidy-plugin"
      ]
      ++ lib.optionals enableDocs [
        "nix-manual"
        "nix-manual-manpages-only"
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
        enableDocs = true;
      } (_: null);
      actualPkgs = lib.concatMapAttrs (
        k: v: if lib.strings.hasPrefix "nix-" k then { ${k} = null; } else { }
      ) nixComponentsFor.${arbitrarySystem}.native;
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
    pkgName:
    lib.filterAttrs (
      system: _do_not_touch:
      pkgName == "nix-nswrapper" -> nixpkgsFor.${system}.native.stdenv.hostPlatform.isLinux
    ) (forAllSystems (system: nixComponentsFor.${system}.native.${pkgName}))
  );

  shellInputs = removeAttrs (forAllSystems (
    system: self.devShells.${system}.default.inputDerivation
  )) [ "i686-linux" ];

  buildStatic = forAllPackages (
    pkgName: lib.genAttrs linux64BitSystems (system: nixComponentsFor.${system}.nativeStatic.${pkgName})
  );

  buildCross = forAllPackages (
    pkgName:
    # Hack to avoid non-evaling package
    (
      if pkgName == "nix-functional-tests" then
        lib.flip builtins.removeAttrs [ "x86_64-w64-mingw32" ]
      else if pkgName == "nix-nswrapper" then
        lib.filterAttrs (
          crossSystem: _do_not_touch: nixpkgsFor.x86_64-linux.cross.${crossSystem}.stdenv.hostPlatform.isLinux
        )
      else
        lib.id
    )
      (
        forAllCrossSystems (
          crossSystem:
          lib.genAttrs [ "x86_64-linux" ] (system: nixComponentsFor.${system}.cross.${crossSystem}.${pkgName})
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
          components = nixComponentsFor.${system}.native;
          pkgs = components._pkgs;
        in
        nixComponentsFor.${system}.native.overrideScope (
          self: super: {
            # Boost coroutines fail with ASAN on darwin.
            withASan = !pkgs.stdenv.buildPlatform.isDarwin;
            withUBSan = true;
            # Build without unity to catch include issues.
            withUnityBuild = false;
            nix-expr = super.nix-expr.override { enableGC = false; };
          }
        )
      );
    in
    forAllPackages (
      pkgName:
      lib.filterAttrs (
        system: _do_not_touch:
        pkgName == "nix-nswrapper" -> nixpkgsFor.${system}.native.stdenv.hostPlatform.isLinux
      ) (forAllSystems (system: components.${system}.${pkgName}))
    );

  # Separate build because one cannot mix ASan + UBSan with TSan.
  buildWithTSan =
    let
      components =
        system:
        nixComponentsFor.${system}.native.overrideScope (
          self: super: {
            withTSan = true;
            # TSan has issues with fork and threads.
            nix-functional-tests = super.nix-functional-tests.overrideAttrs { doCheck = false; };
          }
        );
    in
    forAllPackages (pkgName: lib.genAttrs linux64BitSystems (system: (components system).${pkgName}));

  # Static analysis with clang-tidy
  clangTidy = lib.genAttrs linux64BitSystems (
    system:
    let
      tidyScope = nixComponentsFor.${system}.nativeForStdenv.clangStdenv.overrideScope (
        self: super: {
          withClangTidy = true;
          # clang-tidy doesn't seem to like unity builds.
          withUnityBuild = false;
          # nix-everything is built via callPackage (not the layer system), so
          # enableClangTidyLayer's doCheck=false doesn't reach it. Set it here
          # so checkInputs (the *-tests.tests.run derivations) aren't pulled in.
          nix-everything = super.nix-everything.overrideAttrs { doCheck = false; };
        }
      );
    in
    tidyScope.nix-everything
  );

  buildNoTests = forAllSystems (system: nixComponentsFor.${system}.native.nix-cli);

  # Toggles some settings for better coverage. Windows needs these
  # library combinations, and Debian build Nix with GNU readline too.
  buildReadlineNoMarkdown =
    let
      components = forAllSystems (
        system:
        nixComponentsFor.${system}.native.overrideScope (
          self: super: {
            nix-cmd = super.nix-cmd.override {
              enableMarkdown = false;
              readlineFlavor = "readline";
            };
          }
        )
      );
    in
    forAllPackages (
      pkgName:
      lib.filterAttrs (
        system: _do_not_touch:
        pkgName == "nix-nswrapper" -> nixpkgsFor.${system}.native.stdenv.hostPlatform.isLinux
      ) (forAllSystems (system: components.${system}.${pkgName}))
    );

  /**
    Config with the fewest libraries, so that dependency optionality doesn't regress.
  */
  buildMinimal =
    let
      components = forAllSystems (
        system:
        nixComponentsFor.${system}.native.overrideScope (
          self: super: {
            # No Boehm GC (libgc).
            nix-expr = super.nix-expr.override { enableGC = false; };
            # No Markdown rendering (lowdown).
            nix-cmd = super.nix-cmd.override { enableMarkdown = false; };
            # No S3 auth (aws-crt-cpp).
            nix-store = super.nix-store.override { withAWS = false; };
            # No mimalloc, no plugin C API.
            nix-cli = super.nix-cli.override {
              withMimalloc = false;
              withPluginCApi = false;
            };
            # Also, temporarily stuff newer libgit2 for more coverage of ifdefs.
            # Would be nicer for this to be somehow a private input, but we can't
            # override nixDependencies easily.
            libgit2 = self.callPackage (
              { pkgs, fetchFromGitHub }:
              pkgs.libgit2.overrideAttrs {
                version = "2.0.0-rc.1";
                src = fetchFromGitHub {
                  owner = "libgit2";
                  repo = "libgit2";
                  rev = "ae45d0d168f7e8dbfdb8c623589cb51caac96ab3";
                  hash = "sha256-3sbqHm37SOwBeFgtjI2DLN6kx1F7G2N1m6rRIkqDXNI=";
                };
              }
            ) { };
          }
        )
      );
    in
    forAllPackages (
      pkgName:
      lib.filterAttrs (
        system: _do_not_touch:
        pkgName == "nix-nswrapper" -> nixpkgsFor.${system}.native.stdenv.hostPlatform.isLinux
      ) (forAllSystems (system: components.${system}.${pkgName}))
    );

  # Binary tarball for various platforms, containing a Nix store
  # with the closure of 'nix' package, and the second half of
  # the installation script.
  binaryTarball = forAllSystems (
    system: nixComponentsFor.${system}.native.callPackage ./binary-tarball.nix { }
  );

  binaryTarballCross = lib.genAttrs [ "x86_64-linux" ] (
    system:
    forAllCrossSystems (
      crossSystem: nixComponentsFor.${system}.cross.${crossSystem}.callPackage ./binary-tarball.nix { }
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
    self.hydraJobs.binaryTarball."aarch64-darwin"
    # Cross
    self.hydraJobs.binaryTarballCross."x86_64-linux"."armv6l-unknown-linux-gnueabihf"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."armv7l-unknown-linux-gnueabihf"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."powerpc64-unknown-linux-gnuabielfv1"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."powerpc64le-unknown-linux-gnu"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."riscv64-unknown-linux-gnu"
    self.hydraJobs.binaryTarballCross."x86_64-linux"."x86_64-unknown-freebsd"
  ];

  # TODO: Shouldn't even be part of hydraJobs. No tarballs should actually be needed
  # because those get taken from the --tarball-url-prefix argument.
  installerScriptForGHA = forAllSystems (
    system:
    nixpkgsFor.${system}.native.callPackage ./installer {
      tarballs = [ self.hydraJobs.binaryTarball.${system} ];
      # Platform doesn't matter, we only need to fish out the fineVersion.
      version = nixComponentsFor.x86_64-linux.native.nix-cli.version;
    }
  );

  # `NixOS/nix-installer` with this revision's Nix closure embedded.
  rustInstaller = lib.genAttrs (linux64BitSystems ++ [ "aarch64-darwin" ]) (
    system:
    let
      components = nixComponentsFor.${system}.native;
      pkgs = components._pkgs;
      # Embed the native (glibc) Nix even though the Linux installer
      # binary is static/musl.
      tarball = pkgs.callPackage ./rust-installer/tarball.nix {
        # TODO: Shouldn't this be nix-cli?
        nix = components.nix-everything;
      };
      builder = if pkgs.stdenv.hostPlatform.isLinux then pkgs.pkgsStatic else pkgs;
    in
    builder.callPackage ./rust-installer {
      inherit tarball;
    }
  );

  /**
    Docker image with Nix inside.
  */
  dockerImage = lib.genAttrs linux64BitSystems (
    system:
    let
      components = nixComponentsFor.${system}.native;
      pkgs = components._pkgs;
      image = pkgs.callPackage ../docker.nix {
        tag = components.nix-cli.version;
        # Override the nix used at build time to create the local store db. This is
        # the intended way to do this since https://github.com/NixOS/nixpkgs/pull/561007.
        # We are not doing cross builds yet, but splicing machinery should just work (tm)
        # if we do start.
        dockerTools = pkgs.dockerTools.override { nix = components.nix-cli; };
        nix = components.nix-cli;
      };
    in
    pkgs.runCommand "docker-image-tarball-${components.nix-cli.version}"
      { meta.description = "Docker image with Nix for ${system}"; }
      ''
        mkdir -p $out/nix-support
        image=$out/image.tar.gz
        ln -s ${image} $image
        echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
      ''
  );

  # Line coverage analysis.
  coverage =
    (import ./../ci/gha/tests rec {
      withCoverage = true;
      pkgs = nixComponents._pkgs;
      nixComponents = nixComponentsFor.x86_64-linux.nativeForStdenv.clangStdenv;
      nixFlake = null;
      getStdenv = p: p.clangStdenv;
    }).codeCoverage.coverageReports.overrideAttrs
      {
        name = "nix-coverage"; # For historical consistency
      };

  /**
    Nix's manual
  */
  manual = nixComponentsFor.x86_64-linux.native.nix-manual;

  /**
    API docs for Nix's unstable internal C++ interfaces.
  */
  internal-api-docs = nixComponentsFor.x86_64-linux.native.nix-internal-api-docs;

  /**
    API docs for Nix's C bindings.
  */
  external-api-docs = nixComponentsFor.x86_64-linux.native.nix-external-api-docs;

  # System tests.
  tests =
    import ../tests/nixos rec {
      inherit lib nixpkgs;
      nixComponents = nixComponentsFor.x86_64-linux.native;
      pkgs = nixComponents._pkgs;
      inherit (self.inputs) nixpkgs-23-11;
    }
    // {

      # Make sure that nix-env still produces the exact same result
      # on a particular version of Nixpkgs.
      evalNixpkgs =
        let
          components = nixComponentsFor.x86_64-linux.native;
          inherit (components._pkgs) runCommand;
        in
        runCommand "eval-nixos" { buildInputs = [ components.nix-cli ]; } ''
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
        let
          components = nixComponentsFor.${system}.native;
          pkgs = components._pkgs;
        in
        import (nixpkgs + "/lib/tests/test-with-nix.nix") {
          inherit (pkgs) lib;
          inherit pkgs;
          nix = components.nix-cli;
        }
      );

      filetransfer-retry-backoff = forAllSystems (
        system: nixComponentsFor.${system}.native.callPackage ../tests/filetransfer-retry-backoff { }
      );

      /**
        Run functional tests with against set of nix daemon versions to catch
        protocol incompatibilities.
      */
      daemonCompat = forAllSystems (
        system:
        let
          components = nixComponentsFor.${system}.native;
          pkgs = components._pkgs;
        in
        pkgs.runCommand "daemon-compat-tests" {
          againstSelf = testNixVersions components components.nix-cli;
          againstCurrentLatest = testNixVersions components pkgs.nixVersions.latest;
          againstLatestStable = testNixVersions components pkgs.nixVersions.stable;
        } "touch $out"
      );

      /**
        Test the installer in QEMU VMs. Doesn't operate on the user-facing
        installation script https://nixos.org/nix/install, which downloads
        the binaries for the system architecture but the second-stage tarballs
        directly.
      */
      installer = import ../tests/installer {
        binaryTarballs = self.hydraJobs.binaryTarball;
        inherit nixpkgsFor;
        inherit lib;
      };
    };

  metrics.nixpkgs =
    let
      components = nixComponentsFor.x86_64-linux.native;
    in
    import "${nixpkgs-regression}/pkgs/top-level/metrics.nix" {
      nixpkgs = nixpkgs-regression;
      pkgs = components._pkgs // {
        nix = components.nix-cli;
      };
    };
}
