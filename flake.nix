{
  description = "The purely functional package manager";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  inputs.nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.nixpkgs-23-11.url = "github:NixOS/nixpkgs/a62e6edd6d5e1fa0329b8653c801147986f8d446";
  inputs.flake-compat = {
    url = "github:edolstra/flake-compat";
    flake = false;
  };

  # dev tooling
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";
  inputs.git-hooks-nix.url = "github:cachix/git-hooks.nix";
  # work around https://github.com/NixOS/nix/issues/7730
  inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
  inputs.git-hooks-nix.inputs.nixpkgs.follows = "nixpkgs";
  inputs.git-hooks-nix.inputs.nixpkgs-stable.follows = "nixpkgs";
  # work around 7730 and https://github.com/NixOS/nix/issues/7807
  inputs.git-hooks-nix.inputs.flake-compat.follows = "";
  inputs.git-hooks-nix.inputs.gitignore.follows = "";

  outputs =
    inputs@{
      self,
      nixpkgs,
      nixpkgs-regression,
      ...
    }:

    let
      inherit (nixpkgs) lib;

      officialRelease = true;

      linux32BitSystems = [ "i686-linux" ];
      linux64BitSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      linuxSystems = linux32BitSystems ++ linux64BitSystems;
      darwinSystems = [
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      systems = linuxSystems ++ darwinSystems;

      crossSystems = [
        "armv6l-unknown-linux-gnueabihf"
        "armv7l-unknown-linux-gnueabihf"
        "riscv64-unknown-linux-gnu"
        # Disabled because of https://github.com/NixOS/nixpkgs/issues/344423
        # "x86_64-unknown-netbsd"
        "x86_64-unknown-freebsd"
        "x86_64-w64-mingw32"
      ];

      stdenvs = [
        "ccacheStdenv"
        "clangStdenv"
        "gccStdenv"
        "libcxxStdenv"
        "stdenv"
      ];

      /**
        `flatMapAttrs attrs f` applies `f` to each attribute in `attrs` and
        merges the results into a single attribute set.

        This can be nested to form a build matrix where all the attributes
        generated by the innermost `f` are returned as is.
        (Provided that the names are unique.)

        See https://nixos.org/manual/nixpkgs/stable/index.html#function-library-lib.attrsets.concatMapAttrs
      */
      flatMapAttrs = attrs: f: lib.concatMapAttrs f attrs;

      forAllSystems = lib.genAttrs systems;

      forAllCrossSystems = lib.genAttrs crossSystems;

      forAllStdenvs = lib.genAttrs stdenvs;

      # We don't apply flake-parts to the whole flake so that non-development attributes
      # load without fetching any development inputs.
      devFlake = inputs.flake-parts.lib.mkFlake { inherit inputs; } {
        imports = [ ./maintainers/flake-module.nix ];
        systems = lib.subtractLists crossSystems systems;
        perSystem =
          { system, ... }:
          {
            _module.args.pkgs = nixpkgsFor.${system}.native;
          };
      };

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor = forAllSystems (
        system:
        let
          make-pkgs =
            crossSystem:
            forAllStdenvs (
              stdenv:
              import nixpkgs {
                localSystem = {
                  inherit system;
                };
                crossSystem =
                  if crossSystem == null then
                    null
                  else
                    {
                      config = crossSystem;
                    }
                    // lib.optionalAttrs (crossSystem == "x86_64-unknown-freebsd13") {
                      useLLVM = true;
                    };
                overlays = [
                  (overlayFor (pkgs: pkgs.${stdenv}))
                ];
              }
            );
        in
        rec {
          nativeForStdenv = make-pkgs null;
          crossForStdenv = forAllCrossSystems make-pkgs;
          # Alias for convenience
          native = nativeForStdenv.stdenv;
          cross = forAllCrossSystems (crossSystem: crossForStdenv.${crossSystem}.stdenv);
        }
      );

      overlayFor =
        getStdenv: final: prev:
        let
          stdenv = getStdenv final;
        in
        {
          nixStable = prev.nix;

          # A new scope, so that we can use `callPackage` to inject our own interdependencies
          # without "polluting" the top level "`pkgs`" attrset.
          # This also has the benefit of providing us with a distinct set of packages
          # we can iterate over.
          nixComponents =
            lib.makeScopeWithSplicing'
              {
                inherit (final) splicePackages;
                inherit (final.nixDependencies) newScope;
              }
              {
                otherSplices = final.generateSplicesForMkScope "nixComponents";
                f = import ./packaging/components.nix {
                  inherit (final) lib;
                  inherit officialRelease;
                  pkgs = final;
                  src = self;
                  maintainers = [ ];
                };
              };

          # The dependencies are in their own scope, so that they don't have to be
          # in Nixpkgs top level `pkgs` or `nixComponents`.
          nixDependencies =
            lib.makeScopeWithSplicing'
              {
                inherit (final) splicePackages;
                inherit (final) newScope; # layered directly on pkgs, unlike nixComponents above
              }
              {
                otherSplices = final.generateSplicesForMkScope "nixDependencies";
                f = import ./packaging/dependencies.nix {
                  inherit stdenv;
                  pkgs = final;
                };
              };

          nix = final.nixComponents.nix-cli;

          # See https://github.com/NixOS/nixpkgs/pull/214409
          # Remove when fixed in this flake's nixpkgs
          pre-commit =
            if prev.stdenv.hostPlatform.system == "i686-linux" then
              (prev.pre-commit.override (o: {
                dotnet-sdk = "";
              })).overridePythonAttrs
                (o: {
                  doCheck = false;
                })
            else
              prev.pre-commit;
        };

    in
    {
      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix-perl-bindings' packages.
      overlays.default = overlayFor (p: p.stdenv);

      hydraJobs = import ./packaging/hydra.nix {
        inherit
          inputs
          forAllCrossSystems
          forAllSystems
          lib
          linux64BitSystems
          nixpkgsFor
          self
          officialRelease
          ;
      };

      checks = forAllSystems (
        system:
        {
          installerScriptForGHA = self.hydraJobs.installerScriptForGHA.${system};
          installTests = self.hydraJobs.installTests.${system};
          nixpkgsLibTests = self.hydraJobs.tests.nixpkgsLibTests.${system};
          rl-next =
            let
              pkgs = nixpkgsFor.${system}.native;
            in
            pkgs.buildPackages.runCommand "test-rl-next-release-notes" { } ''
              LANG=C.UTF-8 ${pkgs.changelog-d}/bin/changelog-d ${./doc/manual/rl-next} >$out
            '';
          repl-completion = nixpkgsFor.${system}.native.callPackage ./tests/repl-completion.nix { };

          /**
            Checks for our packaging expressions.
            This shouldn't build anything significant; just check that things
            (including derivations) are _set up_ correctly.
          */
          # Disabled due to a bug in `testEqualContents` (see
          # https://github.com/NixOS/nix/issues/12690).
          /*
            packaging-overriding =
              let
                pkgs = nixpkgsFor.${system}.native;
                nix = self.packages.${system}.nix;
              in
              assert (nix.appendPatches [ pkgs.emptyFile ]).libs.nix-util.src.patches == [ pkgs.emptyFile ];
              if pkgs.stdenv.buildPlatform.isDarwin then
                lib.warn "packaging-overriding check currently disabled because of a permissions issue on macOS" pkgs.emptyFile
              else
                # If this fails, something might be wrong with how we've wired the scope,
                # or something could be broken in Nixpkgs.
                pkgs.testers.testEqualContents {
                  assertion = "trivial patch does not change source contents";
                  expected = "${./.}";
                  actual =
                    # Same for all components; nix-util is an arbitrary pick
                    (nix.appendPatches [ pkgs.emptyFile ]).libs.nix-util.src;
                };
          */
        }
        // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
          dockerImage = self.hydraJobs.dockerImage.${system};
        }
        // (lib.optionalAttrs (!(builtins.elem system linux32BitSystems))) {
          # Some perl dependencies are broken on i686-linux.
          # Since the support is only best-effort there, disable the perl
          # bindings
          perlBindings = self.hydraJobs.perlBindings.${system};
        }
        # Add "passthru" tests
        //
          flatMapAttrs
            (
              {
                # Run all tests with UBSAN enabled. Running both with ubsan and
                # without doesn't seem to have much immediate benefit for doubling
                # the GHA CI workaround.
                #
                # TODO: Work toward enabling "address,undefined" if it seems feasible.
                # This would maybe require dropping Boost coroutines and ignoring intentional
                # memory leaks with detect_leaks=0.
                "" = rec {
                  nixpkgs = nixpkgsFor.${system}.native;
                  nixComponents = nixpkgs.nixComponents.overrideScope (
                    nixCompFinal: nixCompPrev: {
                      mesonComponentOverrides = _finalAttrs: prevAttrs: {
                        mesonFlags =
                          (prevAttrs.mesonFlags or [ ])
                          # TODO: Macos builds instrumented with ubsan take very long
                          # to run functional tests.
                          ++ lib.optionals (!nixpkgs.stdenv.hostPlatform.isDarwin) [
                            (lib.mesonOption "b_sanitize" "undefined")
                          ];
                      };
                    }
                  );
                };
              }
              // lib.optionalAttrs (!nixpkgsFor.${system}.native.stdenv.hostPlatform.isDarwin) {
                # TODO: enable static builds for darwin, blocked on:
                #       https://github.com/NixOS/nixpkgs/issues/320448
                # TODO: disabled to speed up GHA CI.
                # "static-" = {
                #   nixpkgs = nixpkgsFor.${system}.native.pkgsStatic;
                # };
              }
            )
            (
              nixpkgsPrefix:
              {
                nixpkgs,
                nixComponents ? nixpkgs.nixComponents,
              }:
              flatMapAttrs nixComponents (
                pkgName: pkg:
                flatMapAttrs pkg.tests or { } (
                  testName: test: {
                    "${nixpkgsPrefix}${pkgName}-${testName}" = test;
                  }
                )
              )
              // lib.optionalAttrs (nixpkgs.stdenv.hostPlatform == nixpkgs.stdenv.buildPlatform) {
                "${nixpkgsPrefix}nix-functional-tests" = nixComponents.nix-functional-tests;
              }
            )
        // devFlake.checks.${system} or { }
      );

      packages = forAllSystems (
        system:
        {
          # Here we put attributes that map 1:1 into packages.<system>, ie
          # for which we don't apply the full build matrix such as cross or static.
          inherit (nixpkgsFor.${system}.native)
            changelog-d
            ;
          default = self.packages.${system}.nix;
          installerScriptForGHA = self.hydraJobs.installerScriptForGHA.${system};
          binaryTarball = self.hydraJobs.binaryTarball.${system};
          # TODO probably should be `nix-cli`
          nix = self.packages.${system}.nix-everything;
          nix-manual = nixpkgsFor.${system}.native.nixComponents.nix-manual;
          nix-internal-api-docs = nixpkgsFor.${system}.native.nixComponents.nix-internal-api-docs;
          nix-external-api-docs = nixpkgsFor.${system}.native.nixComponents.nix-external-api-docs;
        }
        # We need to flatten recursive attribute sets of derivations to pass `flake check`.
        //
          flatMapAttrs
            {
              # Components we'll iterate over in the upcoming lambda
              "nix-util" = { };
              "nix-util-c" = { };
              "nix-util-test-support" = { };
              "nix-util-tests" = { };

              "nix-store" = { };
              "nix-store-c" = { };
              "nix-store-test-support" = { };
              "nix-store-tests" = { };

              "nix-fetchers" = { };
              "nix-fetchers-tests" = { };

              "nix-expr" = { };
              "nix-expr-c" = { };
              "nix-expr-test-support" = { };
              "nix-expr-tests" = { };

              "nix-flake" = { };
              "nix-flake-tests" = { };

              "nix-main" = { };
              "nix-main-c" = { };

              "nix-cmd" = { };

              "nix-cli" = { };

              "nix-everything" = { };

              "nix-functional-tests" = {
                supportsCross = false;
              };

              "nix-perl-bindings" = {
                supportsCross = false;
              };
            }
            (
              pkgName:
              {
                supportsCross ? true,
              }:
              {
                # These attributes go right into `packages.<system>`.
                "${pkgName}" = nixpkgsFor.${system}.native.nixComponents.${pkgName};
                "${pkgName}-static" = nixpkgsFor.${system}.native.pkgsStatic.nixComponents.${pkgName};
                "${pkgName}-llvm" = nixpkgsFor.${system}.native.pkgsLLVM.nixComponents.${pkgName};
              }
              // lib.optionalAttrs supportsCross (
                flatMapAttrs (lib.genAttrs crossSystems (_: { })) (
                  crossSystem:
                  { }:
                  {
                    # These attributes go right into `packages.<system>`.
                    "${pkgName}-${crossSystem}" = nixpkgsFor.${system}.cross.${crossSystem}.nixComponents.${pkgName};
                  }
                )
              )
              // flatMapAttrs (lib.genAttrs stdenvs (_: { })) (
                stdenvName:
                { }:
                {
                  # These attributes go right into `packages.<system>`.
                  "${pkgName}-${stdenvName}" =
                    nixpkgsFor.${system}.nativeForStdenv.${stdenvName}.nixComponents.${pkgName};
                }
              )
            )
        // lib.optionalAttrs (builtins.elem system linux64BitSystems) {
          dockerImage =
            let
              pkgs = nixpkgsFor.${system}.native;
              image = import ./docker.nix {
                inherit pkgs;
                tag = pkgs.nix.version;
              };
            in
            pkgs.runCommand "docker-image-tarball-${pkgs.nix.version}"
              { meta.description = "Docker image with Nix for ${system}"; }
              ''
                mkdir -p $out/nix-support
                image=$out/image.tar.gz
                ln -s ${image} $image
                echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
              '';
        }
      );

      devShells =
        let
          makeShell = import ./packaging/dev-shell.nix { inherit lib devFlake; };
          prefixAttrs = prefix: lib.concatMapAttrs (k: v: { "${prefix}-${k}" = v; });
        in
        forAllSystems (
          system:
          prefixAttrs "native" (
            forAllStdenvs (
              stdenvName:
              makeShell {
                pkgs = nixpkgsFor.${system}.nativeForStdenv.${stdenvName};
              }
            )
          )
          // lib.optionalAttrs (!nixpkgsFor.${system}.native.stdenv.isDarwin) (
            prefixAttrs "static" (
              forAllStdenvs (
                stdenvName:
                makeShell {
                  pkgs = nixpkgsFor.${system}.nativeForStdenv.${stdenvName}.pkgsStatic;
                }
              )
            )
            // prefixAttrs "llvm" (
              forAllStdenvs (
                stdenvName:
                makeShell {
                  pkgs = nixpkgsFor.${system}.nativeForStdenv.${stdenvName}.pkgsLLVM;
                }
              )
            )
            // prefixAttrs "cross" (
              forAllCrossSystems (
                crossSystem:
                makeShell {
                  pkgs = nixpkgsFor.${system}.cross.${crossSystem};
                }
              )
            )
          )
          // {
            native = self.devShells.${system}.native-stdenv;
            default = self.devShells.${system}.native;
          }
        );
    };
}
