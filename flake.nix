{
  description = "The purely functional package manager";

  inputs.nixpkgs.url = "nixpkgs/nixos-21.05-small";
  inputs.nixpkgs-regression.url = "nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";
  inputs.lowdown-src = { url = "github:kristapsdz/lowdown"; flake = false; };

  outputs = { self, nixpkgs, nixpkgs-regression, lowdown-src }:

    let

      version = builtins.readFile ./.version + versionSuffix;
      versionSuffix =
        if officialRelease
        then ""
        else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")}_${self.shortRev or "dirty"}";

      officialRelease = true;

      linux64BitSystems = [ "x86_64-linux" "aarch64-linux" ];
      linuxSystems = linux64BitSystems ++ [ "i686-linux" ];
      systems = linuxSystems ++ [ "x86_64-darwin" "aarch64-darwin" ];

      crossSystems = [ "armv6l-linux" "armv7l-linux" ];

      stdenvs = [ "gccStdenv" "clangStdenv" "clang11Stdenv" "stdenv" ];

      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
      forAllSystemsAndStdenvs = f: forAllSystems (system:
        nixpkgs.lib.listToAttrs
          (map
            (n:
            nixpkgs.lib.nameValuePair "${n}Packages" (
              f system n
            )) stdenvs
          )
      );

      forAllStdenvs = stdenvs: f: nixpkgs.lib.genAttrs stdenvs (stdenv: f stdenv);

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor =
        let stdenvsPackages = forAllSystemsAndStdenvs
          (system: stdenv:
            import nixpkgs {
              inherit system;
              overlays = [
                (overlayFor (p: p.${stdenv}))
              ];
            }
          );
        in
        # Add the `stdenvPackages` at toplevel, both because these are the ones
        # we want most of the time and for backwards compatibility
        forAllSystems (system: stdenvsPackages.${system} // stdenvsPackages.${system}.stdenvPackages);

      commonDeps = pkgs: with pkgs; rec {
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
            "--with-boost=${boost}/lib"
            "--with-sandbox-shell=${sh}/bin/busybox"
            "LDFLAGS=-fuse-ld=gold"
          ];


        nativeBuildDeps =
          [
            buildPackages.bison
            buildPackages.flex
            (lib.getBin buildPackages.lowdown-nix)
            buildPackages.mdbook
            buildPackages.autoconf-archive
            buildPackages.autoreconfHook
            buildPackages.pkg-config

            # Tests
            buildPackages.git
            buildPackages.mercurial # FIXME: remove? only needed for tests
            buildPackages.jq
          ]
          ++ lib.optionals stdenv.hostPlatform.isLinux [(buildPackages.util-linuxMinimal or buildPackages.utillinuxMinimal)];

        buildDeps =
          [ curl
            bzip2 xz brotli editline
            openssl sqlite
            libarchive
            boost
            lowdown-nix
            gtest
          ]
          ++ lib.optionals stdenv.isLinux [libseccomp]
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
          ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid;

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
              ];
            }))
            nlohmann_json
          ];

        perlDeps =
          [ perl
            perlPackages.DBDSQLite
          ];
      };

      installScriptFor = systems:
        with nixpkgsFor.x86_64-linux;
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

      testNixVersions = pkgs: client: daemon: with commonDeps pkgs; with pkgs.lib; pkgs.stdenv.mkDerivation {
        NIX_DAEMON_PACKAGE = daemon;
        NIX_CLIENT_PACKAGE = client;
        name =
          "nix-tests"
          + optionalString
            (versionAtLeast daemon.version "2.4pre20211005" &&
             versionAtLeast client.version "2.4pre20211005")
            "-${client.version}-against-${daemon.version}";
        inherit version;

        src = self;

        VERSION_SUFFIX = versionSuffix;

        nativeBuildInputs = nativeBuildDeps;
        buildInputs = buildDeps ++ awsDeps;
        propagatedBuildInputs = propagatedDeps;

        enableParallelBuilding = true;

        dontBuild = true;
        doInstallCheck = true;

        installPhase = ''
          mkdir -p $out
        '';

        installCheckPhase = "make installcheck -j$NIX_BUILD_CORES -l$NIX_BUILD_CORES";
      };

      binaryTarball = buildPackages: nix: pkgs:
        let
          inherit (pkgs) cacert;
          installerClosureInfo = buildPackages.closureInfo { rootPaths = [ nix cacert ]; };
        in

        buildPackages.runCommand "nix-binary-tarball-${version}"
          { #nativeBuildInputs = lib.optional (system != "aarch64-linux") shellcheck;
            meta.description = "Distribution-independent Nix bootstrap binaries for ${pkgs.system}";
          }
          ''
            cp ${installerClosureInfo}/registration $TMPDIR/reginfo
            cp ${./scripts/create-darwin-volume.sh} $TMPDIR/create-darwin-volume.sh
            substitute ${./scripts/install-nix-from-closure.sh} $TMPDIR/install \
              --subst-var-by nix ${nix} \
              --subst-var-by cacert ${cacert}

            substitute ${./scripts/install-darwin-multi-user.sh} $TMPDIR/install-darwin-multi-user.sh \
              --subst-var-by nix ${nix} \
              --subst-var-by cacert ${cacert}
            substitute ${./scripts/install-systemd-multi-user.sh} $TMPDIR/install-systemd-multi-user.sh \
              --subst-var-by nix ${nix} \
              --subst-var-by cacert ${cacert}
            substitute ${./scripts/install-multi-user.sh} $TMPDIR/install-multi-user \
              --subst-var-by nix ${nix} \
              --subst-var-by cacert ${cacert}

            if type -p shellcheck; then
              # SC1090: Don't worry about not being able to find
              #         $nix/etc/profile.d/nix.sh
              shellcheck --exclude SC1090 $TMPDIR/install
              shellcheck $TMPDIR/create-darwin-volume.sh
              shellcheck $TMPDIR/install-darwin-multi-user.sh
              shellcheck $TMPDIR/install-systemd-multi-user.sh

              # SC1091: Don't panic about not being able to source
              #         /etc/profile
              # SC2002: Ignore "useless cat" "error", when loading
              #         .reginfo, as the cat is a much cleaner
              #         implementation, even though it is "useless"
              # SC2116: Allow ROOT_HOME=$(echo ~root) for resolving
              #         root's home directory
              shellcheck --external-sources \
                --exclude SC1091,SC2002,SC2116 $TMPDIR/install-multi-user
            fi

            chmod +x $TMPDIR/install
            chmod +x $TMPDIR/create-darwin-volume.sh
            chmod +x $TMPDIR/install-darwin-multi-user.sh
            chmod +x $TMPDIR/install-systemd-multi-user.sh
            chmod +x $TMPDIR/install-multi-user
            dir=nix-${version}-${pkgs.system}
            fn=$out/$dir.tar.xz
            mkdir -p $out/nix-support
            echo "file binary-dist $fn" >> $out/nix-support/hydra-build-products
            tar cvfJ $fn \
              --owner=0 --group=0 --mode=u+rw,uga+r \
              --absolute-names \
              --hard-dereference \
              --transform "s,$TMPDIR/install,$dir/install," \
              --transform "s,$TMPDIR/create-darwin-volume.sh,$dir/create-darwin-volume.sh," \
              --transform "s,$TMPDIR/reginfo,$dir/.reginfo," \
              --transform "s,$NIX_STORE,$dir/store,S" \
              $TMPDIR/install \
              $TMPDIR/create-darwin-volume.sh \
              $TMPDIR/install-darwin-multi-user.sh \
              $TMPDIR/install-systemd-multi-user.sh \
              $TMPDIR/install-multi-user \
              $TMPDIR/reginfo \
              $(cat ${installerClosureInfo}/store-paths)
          '';

      overlayFor = getStdenv: final: prev:
        let currentStdenv = getStdenv final; in
        {
          nixStable = prev.nix;

          # Forward from the previous stage as we don’t want it to pick the lowdown override
          nixUnstable = prev.nixUnstable;

          nix = with final; with commonDeps pkgs; currentStdenv.mkDerivation {
            name = "nix-${version}";
            inherit version;

            src = self;

            VERSION_SUFFIX = versionSuffix;

            outputs = [ "out" "dev" "doc" ];

            nativeBuildInputs = nativeBuildDeps;
            buildInputs = buildDeps ++ awsDeps;

            propagatedBuildInputs = propagatedDeps;

            disallowedReferences = [ boost ];

            preConfigure =
              ''
                # Copy libboost_context so we don't get all of Boost in our closure.
                # https://github.com/NixOS/nixpkgs/issues/45462
                mkdir -p $out/lib
                cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*} $out/lib
                rm -f $out/lib/*.a
                ${lib.optionalString currentStdenv.isLinux ''
                  chmod u+w $out/lib/*.so.*
                  patchelf --set-rpath $out/lib:${currentStdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
                ''}
                ${lib.optionalString currentStdenv.isDarwin ''
                  for LIB in $out/lib/*.dylib; do
                    chmod u+w $LIB
                    install_name_tool -id $LIB $LIB
                  done
                  install_name_tool -change ${boost}/lib/libboost_system.dylib $out/lib/libboost_system.dylib $out/lib/libboost_thread.dylib
                ''}
              '';

            configureFlags = configureFlags ++
              [ "--sysconfdir=/etc" ];

            enableParallelBuilding = true;

            makeFlags = "profiledir=$(out)/etc/profile.d PRECOMPILE_HEADERS=1";

            doCheck = true;

            installFlags = "sysconfdir=$(out)/etc";

            postInstall = ''
              mkdir -p $doc/nix-support
              echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
              ${lib.optionalString currentStdenv.isDarwin ''
              install_name_tool \
                -change ${boost}/lib/libboost_context.dylib \
                $out/lib/libboost_context.dylib \
                $out/lib/libnixutil.dylib
              ''}
            '';

            doInstallCheck = true;
            installCheckFlags = "sysconfdir=$(out)/etc";

            separateDebugInfo = true;

            strictDeps = true;

            passthru.perl-bindings = with final; currentStdenv.mkDerivation {
              name = "nix-perl-${version}";

              src = self;

              nativeBuildInputs =
                [ buildPackages.autoconf-archive
                  buildPackages.autoreconfHook
                  buildPackages.pkg-config
                ];

              buildInputs =
                [ nix
                  curl
                  bzip2
                  xz
                  pkgs.perl
                  boost
                ]
                ++ lib.optional (currentStdenv.isLinux || currentStdenv.isDarwin) libsodium
                ++ lib.optional currentStdenv.isDarwin darwin.apple_sdk.frameworks.Security;

              configureFlags = ''
                --with-dbi=${perlPackages.DBI}/${pkgs.perl.libPrefix}
                --with-dbd-sqlite=${perlPackages.DBDSQLite}/${pkgs.perl.libPrefix}
              '';

              enableParallelBuilding = true;

              postUnpack = "sourceRoot=$sourceRoot/perl";
            };

          };

          lowdown-nix = with final; currentStdenv.mkDerivation rec {
            name = "lowdown-0.9.0";

            src = lowdown-src;

            outputs = [ "out" "bin" "dev" ];

            nativeBuildInputs = [ buildPackages.which ];

            configurePhase = ''
                ${if (currentStdenv.isDarwin && currentStdenv.isAarch64) then "echo \"HAVE_SANDBOX_INIT=false\" > configure.local" else ""}
                ./configure \
                  PREFIX=${placeholder "dev"} \
                  BINDIR=${placeholder "bin"}/bin
            '';
          };
          nix-find-roots = prev.stdenv.mkDerivation {
            name = "nix-find-roots-${version}";
            inherit version;

            src = "${self}/src/nix-find-roots";

            CXXFLAGS = prev.lib.optionalString prev.stdenv.hostPlatform.isStatic "-static";

            buildPhase = ''
              $CXX $CXXFLAGS -std=c++17 nix-find-roots.cc -o nix-find-roots
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp nix-find-roots $out/bin/
            '';
          };
        };

    in {

      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlay = overlayFor (p: p.stdenv);

      hydraJobs = {

        # Binary package for various platforms.
        build = nixpkgs.lib.genAttrs systems (system: self.packages.${system}.nix);

        buildStatic = nixpkgs.lib.genAttrs linux64BitSystems (system: self.packages.${system}.nix-static);

        buildCross = nixpkgs.lib.genAttrs crossSystems (crossSystem:
          nixpkgs.lib.genAttrs ["x86_64-linux"] (system: self.packages.${system}."nix-${crossSystem}"));

        # Perl bindings for various platforms.
        perlBindings = nixpkgs.lib.genAttrs systems (system: self.packages.${system}.nix.perl-bindings);

        # Binary tarball for various platforms, containing a Nix store
        # with the closure of 'nix' package, and the second half of
        # the installation script.
        binaryTarball = nixpkgs.lib.genAttrs systems (system: binaryTarball nixpkgsFor.${system} nixpkgsFor.${system}.nix nixpkgsFor.${system});

        binaryTarballCross = nixpkgs.lib.genAttrs ["x86_64-linux"] (system: builtins.listToAttrs (map (crossSystem: {
          name = crossSystem;
          value = let
            nixpkgsCross = import nixpkgs {
              inherit system crossSystem;
              overlays = [ self.overlay ];
            };
          in binaryTarball nixpkgsFor.${system} self.packages.${system}."nix-${crossSystem}" nixpkgsCross;
        }) crossSystems));

        # The first half of the installation script. This is uploaded
        # to https://nixos.org/nix/install. It downloads the binary
        # tarball for the user's system and calls the second half of the
        # installation script.
        installerScript = installScriptFor [ "x86_64-linux" "i686-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" "armv6l-linux" "armv7l-linux" ];
        installerScriptForGHA = installScriptFor [ "x86_64-linux" "x86_64-darwin" "armv6l-linux" "armv7l-linux"];

        # docker image with Nix inside
        dockerImage = nixpkgs.lib.genAttrs linux64BitSystems (system: self.packages.${system}.dockerImage);

        # Line coverage analysis.
        coverage =
          with nixpkgsFor.x86_64-linux;
          with commonDeps pkgs;

          releaseTools.coverageAnalysis {
            name = "nix-coverage-${version}";

            src = self;

            enableParallelBuilding = true;

            nativeBuildInputs = nativeBuildDeps;
            buildInputs = buildDeps ++ propagatedDeps ++ awsDeps;

            dontInstall = false;

            doInstallCheck = true;

            lcovFilter = [ "*/boost/*" "*-tab.*" ];

            # We call `dot', and even though we just use it to
            # syntax-check generated dot files, it still requires some
            # fonts.  So provide those.
            FONTCONFIG_FILE = texFunctions.fontsConf;
          };

        # System tests.
        tests.remoteBuilds = import ./tests/remote-builds.nix {
          system = "x86_64-linux";
          inherit nixpkgs;
          inherit (self) overlay;
        };

        tests.nix-copy-closure = import ./tests/nix-copy-closure.nix {
          system = "x86_64-linux";
          inherit nixpkgs;
          inherit (self) overlay;
        };

        tests.nssPreload = (import ./tests/nss-preload.nix rec {
          system = "x86_64-linux";
          inherit nixpkgs;
          inherit (self) overlay;
        });

        tests.githubFlakes = (import ./tests/github-flakes.nix rec {
          system = "x86_64-linux";
          inherit nixpkgs;
          inherit (self) overlay;
        });

        tests.sourcehutFlakes = (import ./tests/sourcehut-flakes.nix rec {
          system = "x86_64-linux";
          inherit nixpkgs;
          inherit (self) overlay;
        });

        tests.setuid = nixpkgs.lib.genAttrs
          ["i686-linux" "x86_64-linux"]
          (system:
            import ./tests/setuid.nix rec {
              inherit nixpkgs system;
              inherit (self) overlay;
            });

        # Make sure that nix-env still produces the exact same result
        # on a particular version of Nixpkgs.
        tests.evalNixpkgs =
          with nixpkgsFor.x86_64-linux;
          runCommand "eval-nixos" { buildInputs = [ nix ]; }
            ''
              type -p nix-env
              # Note: we're filtering out nixos-install-tools because https://github.com/NixOS/nixpkgs/pull/153594#issuecomment-1020530593.
              time nix-env --store dummy:// -f ${nixpkgs-regression} -qaP --drv-path | sort | grep -v nixos-install-tools > packages
              [[ $(sha1sum < packages | cut -c1-40) = ff451c521e61e4fe72bdbe2d0ca5d1809affa733 ]]
              mkdir $out
            '';

        metrics.nixpkgs = import "${nixpkgs-regression}/pkgs/top-level/metrics.nix" {
          pkgs = nixpkgsFor.x86_64-linux;
          nixpkgs = nixpkgs-regression;
        };

        installTests = forAllSystems (system:
          let pkgs = nixpkgsFor.${system}; in
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

      };

      checks = forAllSystems (system: {
        binaryTarball = self.hydraJobs.binaryTarball.${system};
        perlBindings = self.hydraJobs.perlBindings.${system};
        installTests = self.hydraJobs.installTests.${system};
      } // (nixpkgs.lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
        dockerImage = self.hydraJobs.dockerImage.${system};
      });

      packages = forAllSystems (system: {
        inherit (nixpkgsFor.${system}) nix;
      } // (nixpkgs.lib.optionalAttrs (builtins.elem system linux64BitSystems) {
        inherit (nixpkgsFor.${system}.pkgsStatic) nix-find-roots;
        nix-static = let
          nixpkgs = nixpkgsFor.${system}.pkgsStatic;
        in with commonDeps nixpkgs; nixpkgs.stdenv.mkDerivation {
          name = "nix-${version}";

          src = self;

          VERSION_SUFFIX = versionSuffix;

          outputs = [ "out" "dev" "doc" ];

          nativeBuildInputs = nativeBuildDeps;
          buildInputs = buildDeps ++ propagatedDeps;

          configureFlags = [ "--sysconfdir=/etc" ];

          enableParallelBuilding = true;

          makeFlags = "profiledir=$(out)/etc/profile.d";

          doCheck = true;

          installFlags = "sysconfdir=$(out)/etc";

          postInstall = ''
            mkdir -p $doc/nix-support
            echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
            mkdir -p $out/nix-support
            echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
          '';

          doInstallCheck = true;
          installCheckFlags = "sysconfdir=$(out)/etc";

          stripAllList = ["bin"];

          strictDeps = true;

          hardeningDisable = [ "pie" ];
        };
        dockerImage =
          let
            pkgs = nixpkgsFor.${system};
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
      } // builtins.listToAttrs (map (crossSystem: {
        name = "nix-${crossSystem}";
        value = let
          nixpkgsCross = import nixpkgs {
            inherit system crossSystem;
            overlays = [ self.overlay ];
          };
        in with commonDeps nixpkgsCross; nixpkgsCross.stdenv.mkDerivation {
          name = "nix-${version}";

          src = self;

          VERSION_SUFFIX = versionSuffix;

          outputs = [ "out" "dev" "doc" ];

          nativeBuildInputs = nativeBuildDeps;
          buildInputs = buildDeps ++ propagatedDeps;

          configureFlags = [ "--sysconfdir=/etc" "--disable-doc-gen" ];

          enableParallelBuilding = true;

          makeFlags = "profiledir=$(out)/etc/profile.d";

          doCheck = true;

          installFlags = "sysconfdir=$(out)/etc";

          postInstall = ''
            mkdir -p $doc/nix-support
            echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
            mkdir -p $out/nix-support
            echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
          '';

          doInstallCheck = true;
          installCheckFlags = "sysconfdir=$(out)/etc";
        };
      }) crossSystems)) // (builtins.listToAttrs (map (stdenvName:
        nixpkgsFor.${system}.lib.nameValuePair
          "nix-${stdenvName}"
          nixpkgsFor.${system}."${stdenvName}Packages".nix
      ) stdenvs)));

      defaultPackage = forAllSystems (system: self.packages.${system}.nix);

      devShell = forAllSystems (system: self.devShells.${system}.stdenvPackages);

      devShells = forAllSystemsAndStdenvs (system: stdenv:
        with nixpkgsFor.${system};
        with commonDeps pkgs;

        nixpkgsFor.${system}.${stdenv}.mkDerivation {
          name = "nix";

          outputs = [ "out" "dev" "doc" ];

          nativeBuildInputs = nativeBuildDeps;
          buildInputs = buildDeps ++ propagatedDeps ++ awsDeps ++ perlDeps;

          inherit configureFlags;

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
        });

  };
}
