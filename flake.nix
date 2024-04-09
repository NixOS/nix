{
  description = "The purely functional package manager";

  # TODO switch to nixos-23.11-small
  #      https://nixpk.gs/pr-tracker.html?pr=291954
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/release-23.11";
  inputs.nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.flake-compat = { url = "github:edolstra/flake-compat"; flake = false; };
  inputs.libgit2 = { url = "github:libgit2/libgit2"; flake = false; };

  outputs = { self, nixpkgs, nixpkgs-regression, libgit2, ... }:

    let
      inherit (nixpkgs) lib;
      inherit (lib) fileset;

      officialRelease = false;

      version = lib.fileContents ./.version + versionSuffix;
      versionSuffix =
        if officialRelease
        then ""
        else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")}_${self.shortRev or "dirty"}";

      linux32BitSystems = [ "i686-linux" ];
      linux64BitSystems = [ "x86_64-linux" "aarch64-linux" ];
      linuxSystems = linux32BitSystems ++ linux64BitSystems;
      darwinSystems = [ "x86_64-darwin" "aarch64-darwin" ];
      systems = linuxSystems ++ darwinSystems;

      crossSystems = [
        "armv6l-unknown-linux-gnueabihf"
        "armv7l-unknown-linux-gnueabihf"
        "x86_64-unknown-netbsd"
      ];

      # Nix doesn't yet build on this platform, so we put it in a
      # separate list. We just use this for `devShells` and
      # `nixpkgsFor`, which this depends on.
      shellCrossSystems = crossSystems ++ [
        "x86_64-w64-mingw32"
      ];

      stdenvs = [
        "ccacheStdenv"
        "clangStdenv"
        "gccStdenv"
        "libcxxStdenv"
        "stdenv"
      ];

      forAllSystems = lib.genAttrs systems;

      forAllCrossSystems = lib.genAttrs crossSystems;

      forAllStdenvs = f:
        lib.listToAttrs
          (map
            (stdenvName: {
              name = "${stdenvName}Packages";
              value = f stdenvName;
            })
            stdenvs);

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor = forAllSystems
        (system: let
          make-pkgs = crossSystem: stdenv: import nixpkgs {
            localSystem = {
              inherit system;
            };
            crossSystem = if crossSystem == null then null else {
              config = crossSystem;
            } // lib.optionalAttrs (crossSystem == "x86_64-unknown-freebsd13") {
              useLLVM = true;
            };
            overlays = [
              (overlayFor (p: p.${stdenv}))
            ];
          };
          stdenvs = forAllStdenvs (make-pkgs null);
          native = stdenvs.stdenvPackages;
        in {
          inherit stdenvs native;
          static = native.pkgsStatic;
          cross = lib.genAttrs shellCrossSystems (crossSystem: make-pkgs crossSystem "stdenv");
        });

      installScriptFor = tarballs:
        nixpkgsFor.x86_64-linux.native.callPackage ./scripts/installer.nix {
          inherit tarballs;
        };

      testNixVersions = pkgs: client: daemon:
        pkgs.callPackage ./package.nix {
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

      binaryTarball = nix: pkgs: pkgs.callPackage ./scripts/binary-tarball.nix {
        inherit nix;
      };

      overlayFor = getStdenv: final: prev:
        let
          stdenv = getStdenv final;
        in
        {
          nixStable = prev.nix;

          default-busybox-sandbox-shell = final.busybox.override {
            useMusl = true;
            enableStatic = true;
            enableMinimal = true;
            extraConfig = ''
              CONFIG_FEATURE_FANCY_ECHO y
              CONFIG_FEATURE_SH_MATH y
              CONFIG_FEATURE_SH_MATH_64 y

              CONFIG_ASH y
              CONFIG_ASH_OPTIMIZE_FOR_SIZE y

              CONFIG_ASH_ALIAS y
              CONFIG_ASH_BASH_COMPAT y
              CONFIG_ASH_CMDCMD y
              CONFIG_ASH_ECHO y
              CONFIG_ASH_GETOPTS y
              CONFIG_ASH_INTERNAL_GLOB y
              CONFIG_ASH_JOB_CONTROL y
              CONFIG_ASH_PRINTF y
              CONFIG_ASH_TEST y
            '';
          };

          libgit2-nix = final.libgit2.overrideAttrs (attrs: {
            src = libgit2;
            version = libgit2.lastModifiedDate;
            cmakeFlags = attrs.cmakeFlags or []
              ++ [ "-DUSE_SSH=exec" ];
          });

          boehmgc-nix = (final.boehmgc.override {
            enableLargeConfig = true;
          }).overrideAttrs(o: {
            patches = (o.patches or []) ++ [
              ./dep-patches/boehmgc-coroutine-sp-fallback.diff

              # https://github.com/ivmai/bdwgc/pull/586
              ./dep-patches/boehmgc-traceable_allocator-public.diff
            ];
          });

          changelog-d-nix = final.buildPackages.callPackage ./misc/changelog-d.nix { };

          nix =
            let
              officialRelease = false;
              versionSuffix =
                if officialRelease
                then ""
                else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")}_${self.shortRev or "dirty"}";

            in final.callPackage ./package.nix {
              inherit
                fileset
                stdenv
                versionSuffix
                ;
              officialRelease = false;
              boehmgc = final.boehmgc-nix;
              libgit2 = final.libgit2-nix;
              busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
            } // {
              # this is a proper separate downstream package, but put
              # here also for back compat reasons.
              perl-bindings = final.nix-perl-bindings;
            };

          nix-perl-bindings = final.callPackage ./perl {
            inherit fileset stdenv;
          };

        };

    in {
      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlays.default = overlayFor (p: p.stdenv);

      hydraJobs = {

        # Binary package for various platforms.
        build = forAllSystems (system: self.packages.${system}.nix);

        shellInputs = forAllSystems (system: self.devShells.${system}.default.inputDerivation);

        buildStatic = lib.genAttrs linux64BitSystems (system: self.packages.${system}.nix-static);

        buildCross = forAllCrossSystems (crossSystem:
          lib.genAttrs ["x86_64-linux"] (system: self.packages.${system}."nix-${crossSystem}"));

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

        binaryTarballCross = lib.genAttrs ["x86_64-linux"] (system:
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
        ];
        installerScriptForGHA = installScriptFor [
          # Native
          self.hydraJobs.binaryTarball."x86_64-linux"
          self.hydraJobs.binaryTarball."x86_64-darwin"
          # Cross
          self.hydraJobs.binaryTarballCross."x86_64-linux"."armv6l-unknown-linux-gnueabihf"
          self.hydraJobs.binaryTarballCross."x86_64-linux"."armv7l-unknown-linux-gnueabihf"
        ];

        # docker image with Nix inside
        dockerImage = lib.genAttrs linux64BitSystems (system: self.packages.${system}.dockerImage);

        # Line coverage analysis.
        coverage = nixpkgsFor.x86_64-linux.native.nix.override {
          pname = "nix-coverage";
          withCoverageChecks = true;
        };

        # API docs for Nix's unstable internal C++ interfaces.
        internal-api-docs = nixpkgsFor.x86_64-linux.native.callPackage ./package.nix {
          inherit fileset;
          doBuild = false;
          enableInternalAPIDocs = true;
        };

        # API docs for Nix's C bindings.
        external-api-docs = nixpkgsFor.x86_64-linux.native.callPackage ./package.nix {
          inherit fileset;
          doBuild = false;
          enableExternalAPIDocs = true;
        };

        # System tests.
        tests = import ./tests/nixos { inherit lib nixpkgs nixpkgsFor; } // {

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
                { pkgs = nixpkgsFor.${system}.native;
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
          pkgs.runCommand "install-tests" {
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

        installerTests = import ./tests/installer {
          binaryTarballs = self.hydraJobs.binaryTarball;
          inherit nixpkgsFor;
        };

      };

      checks = forAllSystems (system: {
        binaryTarball = self.hydraJobs.binaryTarball.${system};
        installTests = self.hydraJobs.installTests.${system};
        nixpkgsLibTests = self.hydraJobs.tests.nixpkgsLibTests.${system};
        rl-next =
          let pkgs = nixpkgsFor.${system}.native;
          in pkgs.buildPackages.runCommand "test-rl-next-release-notes" { } ''
          LANG=C.UTF-8 ${pkgs.changelog-d-nix}/bin/changelog-d ${./doc/manual/rl-next} >$out
        '';
      } // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
        dockerImage = self.hydraJobs.dockerImage.${system};
      } // (lib.optionalAttrs (!(builtins.elem system linux32BitSystems))) {
        # Some perl dependencies are broken on i686-linux.
        # Since the support is only best-effort there, disable the perl
        # bindings
        perlBindings = self.hydraJobs.perlBindings.${system};
      });

      packages = forAllSystems (system: rec {
        inherit (nixpkgsFor.${system}.native) nix changelog-d-nix;
        default = nix;
      } // (lib.optionalAttrs (builtins.elem system linux64BitSystems) {
        nix-static = nixpkgsFor.${system}.static.nix;
        dockerImage =
          let
            pkgs = nixpkgsFor.${system}.native;
            image = import ./docker.nix { inherit pkgs; tag = version; };
          in
          pkgs.runCommand
            "docker-image-tarball-${version}"
            { meta.description = "Docker image with Nix for ${system}"; }
            ''
              mkdir -p $out/nix-support
              image=$out/image.tar.gz
              ln -s ${image} $image
              echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
            '';
      } // builtins.listToAttrs (map
          (crossSystem: {
            name = "nix-${crossSystem}";
            value = nixpkgsFor.${system}.cross.${crossSystem}.nix;
          })
          crossSystems)
        // builtins.listToAttrs (map
          (stdenvName: {
            name = "nix-${stdenvName}";
            value = nixpkgsFor.${system}.stdenvs."${stdenvName}Packages".nix;
          })
          stdenvs)));

      devShells = let
        makeShell = pkgs: stdenv: (pkgs.nix.override { inherit stdenv; forDevShell = true; }).overrideAttrs (attrs: {
          installFlags = "sysconfdir=$(out)/etc";
          shellHook = ''
            PATH=$prefix/bin:$PATH
            unset PYTHONPATH
            export MANPATH=$out/share/man:$MANPATH

            # Make bash completion work.
            XDG_DATA_DIRS+=:$out/share
          '';

          nativeBuildInputs = attrs.nativeBuildInputs or []
            # TODO: Remove the darwin check once
            # https://github.com/NixOS/nixpkgs/pull/291814 is available
            ++ lib.optional (stdenv.cc.isClang && !stdenv.buildPlatform.isDarwin) pkgs.buildPackages.bear
            ++ lib.optional (stdenv.cc.isClang && stdenv.hostPlatform == stdenv.buildPlatform) pkgs.buildPackages.clang-tools;
        });
        in
        forAllSystems (system:
          let
            makeShells = prefix: pkgs:
              lib.mapAttrs'
              (k: v: lib.nameValuePair "${prefix}-${k}" v)
              (forAllStdenvs (stdenvName: makeShell pkgs pkgs.${stdenvName}));
          in
            (makeShells "native" nixpkgsFor.${system}.native) //
            (lib.optionalAttrs (!nixpkgsFor.${system}.native.stdenv.isDarwin)
              (makeShells "static" nixpkgsFor.${system}.static)) //
              (lib.genAttrs shellCrossSystems (crossSystem: let pkgs = nixpkgsFor.${system}.cross.${crossSystem}; in makeShell pkgs pkgs.stdenv)) //
            {
              default = self.devShells.${system}.native-stdenvPackages;
            }
        );
  };
}
