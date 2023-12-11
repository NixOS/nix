{
  description = "The purely functional package manager";

  # TODO Go back to nixos-23.05-small once
  # https://github.com/NixOS/nixpkgs/pull/271202 is merged.
  #
  # Also, do not grab arbitrary further staging commits. This PR was
  # carefully made to be based on release-23.05 and just contain
  # rebuild-causing changes to packages that Nix actually uses.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/staging-23.05";
  inputs.nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.lowdown-src = { url = "github:kristapsdz/lowdown"; flake = false; };
  inputs.flake-compat = { url = "github:edolstra/flake-compat"; flake = false; };
  inputs.libgit2 = { url = "github:libgit2/libgit2"; flake = false; };

  outputs = { self, nixpkgs, nixpkgs-regression, lowdown-src, libgit2, ... }:

    let
      inherit (nixpkgs) lib;

      # Experimental fileset library: https://github.com/NixOS/nixpkgs/pull/222981
      # Not an "idiomatic" flake input because:
      #  - Propagation to dependent locks: https://github.com/NixOS/nix/issues/7730
      #  - Subflake would download redundant and huge parent flake
      #  - No git tree hash support: https://github.com/NixOS/nix/issues/6044
      inherit (import (builtins.fetchTarball { url = "https://github.com/NixOS/nix/archive/1bdcd7fc8a6a40b2e805bad759b36e64e911036b.tar.gz"; sha256 = "sha256:14ljlpdsp4x7h1fkhbmc4bd3vsqnx8zdql4h3037wh09ad6a0893"; }))
        fileset;

      officialRelease = false;

      # Set to true to build the release notes for the next release.
      buildUnreleasedNotes = false;

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
        "armv6l-linux" "armv7l-linux"
        "x86_64-freebsd13" "x86_64-netbsd"
      ];

      stdenvs = [
        "ccacheStdenv"
        "clang11Stdenv"
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
              system = crossSystem;
            } // lib.optionalAttrs (crossSystem == "x86_64-freebsd13") {
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
          cross = forAllCrossSystems (crossSystem: make-pkgs crossSystem "stdenv");
        });

      installScriptFor = systems:
        nixpkgsFor.x86_64-linux.native.callPackage ./scripts/installer.nix {
          systemTarballPairs = map
            (system: {
              inherit system;
              tarball =
                if builtins.elem system crossSystems
                then self.hydraJobs.binaryTarballCross.x86_64-linux.${system}
                else self.hydraJobs.binaryTarball.${system};
            })
            systems;
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

          # Forward from the previous stage as we don’t want it to pick the lowdown override
          inherit (prev) nixUnstable;

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

          lowdown-nix = final.callPackage ./lowdown.nix {
            inherit lowdown-src stdenv;
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
              ./boehmgc-coroutine-sp-fallback.diff

              # https://github.com/ivmai/bdwgc/pull/586
              ./boehmgc-traceable_allocator-public.diff
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
              lowdown = final.lowdown-nix;
              busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
              changelog-d = final.changelog-d-nix;
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

        buildNoGc = forAllSystems (system: self.packages.${system}.nix.overrideAttrs (a: { configureFlags = (a.configureFlags or []) ++ ["--enable-gc=no"];}));

        buildNoTests = forAllSystems (system:
          self.packages.${system}.nix.overrideAttrs (a: {
            doCheck =
              assert ! a?dontCheck;
              false;
          })
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
          "aarch64-linux"
          "armv6l-linux"
          "armv7l-linux"
          "i686-linux"
          "x86_64-linux"
          "aarch64-darwin"
          "x86_64-darwin"
        ];
        installerScriptForGHA = installScriptFor [
          "armv6l-linux"
          "armv7l-linux"
          "x86_64-linux"
          "x86_64-darwin"
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
                time nix-env --store dummy:// -f ${nixpkgs-regression} -qaP --drv-path | sort | grep -v nixos-install-tools > packages
                [[ $(sha1sum < packages | cut -c1-40) = ff451c521e61e4fe72bdbe2d0ca5d1809affa733 ]]
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
        perlBindings = self.hydraJobs.perlBindings.${system};
        installTests = self.hydraJobs.installTests.${system};
        nixpkgsLibTests = self.hydraJobs.tests.nixpkgsLibTests.${system};
        rl-next =
          let pkgs = nixpkgsFor.${system}.native;
          in pkgs.buildPackages.runCommand "test-rl-next-release-notes" { } ''
          LANG=C.UTF-8 ${pkgs.changelog-d-nix}/bin/changelog-d ${./doc/manual/rl-next} >$out
        '';
      } // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
        dockerImage = self.hydraJobs.dockerImage.${system};
      });

      packages = forAllSystems (system: rec {
        inherit (nixpkgsFor.${system}.native) nix;
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
        makeShell = pkgs: stdenv: (pkgs.nix.override { inherit stdenv; }).overrideAttrs (_: {
          installFlags = "sysconfdir=$(out)/etc";
          shellHook = ''
            PATH=$prefix/bin:$PATH
            unset PYTHONPATH
            export MANPATH=$out/share/man:$MANPATH

            # Make bash completion work.
            XDG_DATA_DIRS+=:$out/share
          '';
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
            (makeShells "static" nixpkgsFor.${system}.static) //
            (forAllCrossSystems (crossSystem: let pkgs = nixpkgsFor.${system}.cross.${crossSystem}; in makeShell pkgs pkgs.stdenv)) //
            {
              default = self.devShells.${system}.native-stdenvPackages;
            }
        );
  };
}
