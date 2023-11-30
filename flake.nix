{
  description = "The purely functional package manager";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-23.05-small";
  inputs.nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.lowdown-src = { url = "github:kristapsdz/lowdown"; flake = false; };
  inputs.flake-compat = { url = "github:edolstra/flake-compat"; flake = false; };
  inputs.libgit2 = { url = "github:libgit2/libgit2"; flake = false; };

  outputs = { self, nixpkgs, nixpkgs-regression, lowdown-src, libgit2, ... }:

    let
      inherit (nixpkgs) lib;

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

      # Experimental fileset library: https://github.com/NixOS/nixpkgs/pull/222981
      # Not an "idiomatic" flake input because:
      #  - Propagation to dependent locks: https://github.com/NixOS/nix/issues/7730
      #  - Subflake would download redundant and huge parent flake
      #  - No git tree hash support: https://github.com/NixOS/nix/issues/6044
      inherit (import (builtins.fetchTarball { url = "https://github.com/NixOS/nix/archive/1bdcd7fc8a6a40b2e805bad759b36e64e911036b.tar.gz"; sha256 = "sha256:14ljlpdsp4x7h1fkhbmc4bd3vsqnx8zdql4h3037wh09ad6a0893"; }))
        fileset;

      baseFiles =
        # .gitignore has already been processed, so any changes in it are irrelevant
        # at this point. It is not represented verbatim for test purposes because
        # that would interfere with repo semantics.
        fileset.fileFilter (f: f.name != ".gitignore") ./.;

      configureFiles = fileset.unions [
        ./.version
        ./configure.ac
        ./m4
        # TODO: do we really need README.md? It doesn't seem used in the build.
        ./README.md
      ];

      topLevelBuildFiles = fileset.unions [
        ./local.mk
        ./Makefile
        ./Makefile.config.in
        ./mk
      ];

      functionalTestFiles = fileset.unions [
        ./tests/functional
        (fileset.fileFilter (f: lib.strings.hasPrefix "nix-profile" f.name) ./scripts)
      ];

      nixSrc = fileset.toSource {
        root = ./.;
        fileset = fileset.intersect baseFiles (fileset.unions [
          configureFiles
          topLevelBuildFiles
          ./boehmgc-coroutine-sp-fallback.diff
          ./doc
          ./misc
          ./precompiled-headers.h
          ./src
          ./unit-test-data
          ./COPYING
          ./scripts/local.mk
          functionalTestFiles
        ]);
      };

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

      commonDeps =
        { pkgs
        , isStatic ? pkgs.stdenv.hostPlatform.isStatic
        }:
        with pkgs; rec {
        # Use "busybox-sandbox-shell" if present,
        # if not (legacy) fallback and hope it's sufficient.
        sh = pkgs.busybox-sandbox-shell or (busybox.override {
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
        });

        configureFlags =
          lib.optionals stdenv.isLinux [
            "--with-boost=${boost-nix}/lib"
            "--with-sandbox-shell=${sh}/bin/busybox"
          ]
          ++ lib.optionals (stdenv.isLinux && !(isStatic && stdenv.system == "aarch64-linux")) [
            "LDFLAGS=-fuse-ld=gold"
          ];

        testConfigureFlags = [
          "RAPIDCHECK_HEADERS=${lib.getDev rapidcheck}/extras/gtest/include"
        ] ++ lib.optionals (stdenv.hostPlatform != stdenv.buildPlatform) [
          "--enable-install-unit-tests"
          "--with-check-bin-dir=${builtins.placeholder "check"}/bin"
          "--with-check-lib-dir=${builtins.placeholder "check"}/lib"
        ];

        internalApiDocsConfigureFlags = [
          "--enable-internal-api-docs"
        ];

        inherit (pkgs.buildPackages) changelog-d;

        nativeBuildDeps =
          [
            buildPackages.bison
            buildPackages.flex
            (lib.getBin buildPackages.lowdown-nix)
            buildPackages.mdbook
            buildPackages.mdbook-linkcheck
            buildPackages.autoconf-archive
            buildPackages.autoreconfHook
            buildPackages.pkg-config

            # Tests
            buildPackages.git
            buildPackages.mercurial # FIXME: remove? only needed for tests
            buildPackages.jq # Also for custom mdBook preprocessor.
            buildPackages.openssh # only needed for tests (ssh-keygen)
          ]
          ++ lib.optionals stdenv.hostPlatform.isLinux [(buildPackages.util-linuxMinimal or buildPackages.utillinuxMinimal)]
          # Official releases don't have rl-next, so we don't need to compile a changelog
          ++ lib.optional (!officialRelease && buildUnreleasedNotes) changelog-d
          ;

        buildDeps =
          [ curl
            bzip2 xz brotli editline
            openssl sqlite
            libarchive
            (pkgs.libgit2.overrideAttrs (attrs: {
              src = libgit2;
              version = libgit2.lastModifiedDate;
              cmakeFlags = (attrs.cmakeFlags or []) ++ ["-DUSE_SSH=exec"];
            }))
            boost-nix
            lowdown-nix
            libsodium
          ]
          ++ lib.optionals stdenv.isLinux [libseccomp]
          ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid;

        checkDeps = [
          gtest
          rapidcheck
        ];

        internalApiDocsDeps = [
          buildPackages.doxygen
        ];

        awsDeps = lib.optional (stdenv.isLinux || stdenv.isDarwin)
          (aws-sdk-cpp.override {
            apis = ["s3" "transfer"];
            customMemoryManagement = false;
          });

        propagatedDeps =
          [ ((boehmgc.override {
              enableLargeConfig = true;
            }).overrideAttrs(o: {
              patches = (o.patches or []) ++ [
                ./boehmgc-coroutine-sp-fallback.diff

                # https://github.com/ivmai/bdwgc/pull/586
                ./boehmgc-traceable_allocator-public.diff
              ];
            })
            )
            nlohmann_json
          ];
      };

      installScriptFor = systems:
        with nixpkgsFor.x86_64-linux.native;
        runCommand "installer-script"
          { buildInputs = [ nix ];
          }
          ''
            mkdir -p $out/nix-support

            # Converts /nix/store/50p3qk8k...-nix-2.4pre20201102_550e11f/bin/nix to 50p3qk8k.../bin/nix.
            tarballPath() {
              # Remove the store prefix
              local path=''${1#${builtins.storeDir}/}
              # Get the path relative to the derivation root
              local rest=''${path#*/}
              # Get the derivation hash
              local drvHash=''${path%%-*}
              echo "$drvHash/$rest"
            }

            substitute ${./scripts/install.in} $out/install \
              ${pkgs.lib.concatMapStrings
                (system: let
                    tarball = if builtins.elem system crossSystems then self.hydraJobs.binaryTarballCross.x86_64-linux.${system} else self.hydraJobs.binaryTarball.${system};
                  in '' \
                  --replace '@tarballHash_${system}@' $(nix --experimental-features nix-command hash-file --base16 --type sha256 ${tarball}/*.tar.xz) \
                  --replace '@tarballPath_${system}@' $(tarballPath ${tarball}/*.tar.xz) \
                  ''
                )
                systems
              } --replace '@nixVersion@' ${version}

            echo "file installer $out/install" >> $out/nix-support/hydra-build-products
          '';

      testNixVersions = pkgs: client: daemon: with commonDeps { inherit pkgs; }; with pkgs.lib; pkgs.stdenv.mkDerivation {
        NIX_DAEMON_PACKAGE = daemon;
        NIX_CLIENT_PACKAGE = client;
        name =
          "nix-tests"
          + optionalString
            (versionAtLeast daemon.version "2.4pre20211005" &&
             versionAtLeast client.version "2.4pre20211005")
            "-${client.version}-against-${daemon.version}";
        inherit version;

        src = fileset.toSource {
          root = ./.;
          fileset = fileset.intersect baseFiles (fileset.unions [
            configureFiles
            topLevelBuildFiles
            functionalTestFiles
          ]);
        };

        VERSION_SUFFIX = versionSuffix;

        nativeBuildInputs = nativeBuildDeps;
        buildInputs = buildDeps ++ awsDeps ++ checkDeps;
        propagatedBuildInputs = propagatedDeps;

        enableParallelBuilding = true;

        configureFlags =
          testConfigureFlags # otherwise configure fails
          ++ [ "--disable-build" ];
        dontBuild = true;
        doInstallCheck = true;

        installPhase = ''
          mkdir -p $out
        '';

        installCheckPhase = ''
          mkdir -p src/nix-channel
          make installcheck -j$NIX_BUILD_CORES -l$NIX_BUILD_CORES
        '';
      };

      binaryTarball = nix: pkgs: pkgs.callPackage ./binary-tarball.nix {
        inherit nix;
      };

      overlayFor = getStdenv: final: prev:
        let
          stdenv = getStdenv final;

          lowdown-nix = final.callPackage ./lowdown.nix {
            inherit lowdown-src stdenv;
          };
        in
        {
          nixStable = prev.nix;

          # Forward from the previous stage as we don’t want it to pick the lowdown override
          inherit (prev) nixUnstable;

          nix =
            let
              officialRelease = false;
              versionSuffix =
                if officialRelease
                then ""
                else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")}_${self.shortRev or "dirty"}";

              sh = final.busybox-sandbox-shell or (final.busybox.override {
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
              });

              boehmgc = (final.boehmgc.override {
                enableLargeConfig = true;
              }).overrideAttrs(o: {
                patches = (o.patches or []) ++ [
                  ./boehmgc-coroutine-sp-fallback.diff

                  # https://github.com/ivmai/bdwgc/pull/586
                  ./boehmgc-traceable_allocator-public.diff
                ];
              });

            in final.callPackage ./package.nix {
              inherit
                boehmgc
                fileset
                sh
                stdenv
                versionSuffix
                ;
              boost = final.boost.override { enableIcu = false; };
              libgit2 = final.libgit2.overrideAttrs (attrs: {
                src = libgit2;
                version = libgit2.lastModifiedDate;
                cmakeFlags = attrs.cmakeFlags or []
                  ++ [ "-DUSE_SSH=exec" ];
              });
              lowdown = lowdown-nix;
              officialRelease = false;
            };

            inherit lowdown-nix;
          };

    in {
      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlays.default = overlayFor (p: p.stdenv);

      hydraJobs = {

        # Binary package for various platforms.
        build = forAllSystems (system: self.packages.${system}.nix);

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
        coverage =
          with nixpkgsFor.x86_64-linux.native;
          with commonDeps { inherit pkgs; };

          releaseTools.coverageAnalysis {
            name = "nix-coverage-${version}";

            src = nixSrc;

            configureFlags = testConfigureFlags;

            enableParallelBuilding = true;

            nativeBuildInputs = nativeBuildDeps;
            buildInputs = buildDeps ++ propagatedDeps ++ awsDeps ++ checkDeps;

            dontInstall = false;

            doInstallCheck = true;
            installCheckTarget = "installcheck"; # work around buggy detection in stdenv

            lcovFilter = [ "*/boost/*" "*-tab.*" ];

            hardeningDisable = ["fortify"];

            NIX_CFLAGS_COMPILE = "-DCOVERAGE=1";
          };

        # API docs for Nix's unstable internal C++ interfaces.
        internal-api-docs =
          with nixpkgsFor.x86_64-linux.native;
          with commonDeps { inherit pkgs; };

          stdenv.mkDerivation {
            pname = "nix-internal-api-docs";
            inherit version;

            src = nixSrc;

            configureFlags = testConfigureFlags ++ internalApiDocsConfigureFlags;

            nativeBuildInputs = nativeBuildDeps;
            buildInputs = buildDeps ++ propagatedDeps
              ++ awsDeps ++ checkDeps ++ internalApiDocsDeps;

            dontBuild = true;

            installTargets = [ "internal-api-html" ];

            postInstall = ''
              mkdir -p $out/nix-support
              echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> $out/nix-support/hydra-build-products
            '';
          };

        # System tests.
        tests = import ./tests/nixos { inherit lib nixpkgs nixpkgsFor; } // {

          # Make sure that nix-env still produces the exact same result
          # on a particular version of Nixpkgs.
          evalNixpkgs =
            with nixpkgsFor.x86_64-linux.native;
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
        makeShell = pkgs: stdenv:
          let
            canRunInstalled = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
          in
          with commonDeps { inherit pkgs; };
          stdenv.mkDerivation {
            name = "nix";

            outputs = [ "out" "dev" "doc" ]
              ++ lib.optional (stdenv.hostPlatform != stdenv.buildPlatform) "check";

            nativeBuildInputs = nativeBuildDeps
              ++ lib.optional stdenv.cc.isClang pkgs.buildPackages.bear
              ++ lib.optional
                (stdenv.cc.isClang && stdenv.hostPlatform == stdenv.buildPlatform)
                pkgs.buildPackages.clang-tools
              # We want changelog-d in the shell even if the current build doesn't need it
              ++ lib.optional (officialRelease || ! buildUnreleasedNotes) changelog-d
              ;

            buildInputs = buildDeps ++ propagatedDeps
              ++ awsDeps ++ checkDeps ++ internalApiDocsDeps;

            configureFlags = configureFlags
              ++ testConfigureFlags ++ internalApiDocsConfigureFlags
              ++ lib.optional (!canRunInstalled) "--disable-doc-gen";

            enableParallelBuilding = true;

            installFlags = "sysconfdir=$(out)/etc";

            shellHook =
              ''
                PATH=$prefix/bin:$PATH
                unset PYTHONPATH
                export MANPATH=$out/share/man:$MANPATH

                # Make bash completion work.
                XDG_DATA_DIRS+=:$out/share
              '';
          };
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
