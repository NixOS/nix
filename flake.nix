{
  description = "The purely functional package manager";

  inputs.nixpkgs.url = "nixpkgs/nixos-21.05-small";
  inputs.lowdown-src = { url = "github:kristapsdz/lowdown/VERSION_0_8_4"; flake = false; };

  outputs = { self, nixpkgs, lowdown-src }:

    let

      version = builtins.readFile ./.version + versionSuffix;
      versionSuffix =
        if officialRelease
        then ""
        else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")}_${self.shortRev or "dirty"}";

      officialRelease = false;

      linux64BitSystems = [ "x86_64-linux" "aarch64-linux" ];
      linuxSystems = linux64BitSystems ++ [ "i686-linux" ];
      systems = linuxSystems ++ [ "x86_64-darwin" "aarch64-darwin" ];

      crossSystems = [ "armv6l-linux" "armv7l-linux" ];

      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor = forAllSystems (system:
        import nixpkgs {
          inherit system;
          overlays = [ self.overlay ];
        }
      );

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
            "--with-sandbox-shell=${sh}/bin/busybox"
            "LDFLAGS=-fuse-ld=gold"
          ];


        nativeBuildDeps =
          [
            buildPackages.bison
            buildPackages.flex
            (lib.getBin buildPackages.lowdown)
            buildPackages.mdbook
            buildPackages.autoconf-archive
            buildPackages.autoreconfHook
            buildPackages.pkgconfig

            # Tests
            buildPackages.git
            buildPackages.mercurial
            buildPackages.jq
          ]
          ++ lib.optionals stdenv.hostPlatform.isLinux [(buildPackages.util-linuxMinimal or buildPackages.utillinuxMinimal)];

        buildDeps =
          [ curl
            bzip2 xz brotli editline
            openssl sqlite
            libarchive
            boost
            lowdown
            gmock
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

            # Converts /nix/store/50p3qk8kka9dl6wyq40vydq945k0j3kv-nix-2.4pre20201102_550e11f/bin/nix
            # To 50p3qk8kka9dl6wyq40vydq945k0j3kv/bin/nix
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

      testNixVersions = pkgs: client: daemon: with commonDeps pkgs; pkgs.stdenv.mkDerivation {
        NIX_DAEMON_PACKAGE = daemon;
        NIX_CLIENT_PACKAGE = client;
        # Must keep this name short as OSX has a rather strict limit on the
        # socket path length, and this name appears in the path of the
        # nix-daemon socket used in the tests
        name = "nix-tests";
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
        installCheckPhase = "make installcheck";

      };

      binaryTarball = buildPackages: nix: pkgs: let
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

    in {

      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlay = final: prev: {

        # An older version of Nix to test against when using the daemon.
        # Currently using `nixUnstable` as the stable one doesn't respect
        # `NIX_DAEMON_SOCKET_PATH` which is needed for the tests.
        nixStable = prev.nix;

        nix = with final; with commonDeps pkgs; stdenv.mkDerivation {
          name = "nix-${version}";
          inherit version;

          src = self;

          VERSION_SUFFIX = versionSuffix;

          outputs = [ "out" "dev" "doc" ];

          nativeBuildInputs = nativeBuildDeps;
          buildInputs = buildDeps ++ awsDeps;

          propagatedBuildInputs = propagatedDeps;

          preConfigure =
            ''
              # Copy libboost_context so we don't get all of Boost in our closure.
              # https://github.com/NixOS/nixpkgs/issues/45462
              mkdir -p $out/lib
              cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*} $out/lib
              rm -f $out/lib/*.a
              ${lib.optionalString stdenv.isLinux ''
                chmod u+w $out/lib/*.so.*
                patchelf --set-rpath $out/lib:${stdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
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
          '';

          doInstallCheck = true;
          installCheckFlags = "sysconfdir=$(out)/etc";

          separateDebugInfo = true;

          strictDeps = true;

          passthru.perl-bindings = with final; stdenv.mkDerivation {
            name = "nix-perl-${version}";

            src = self;

            nativeBuildInputs =
              [ buildPackages.autoconf-archive
                buildPackages.autoreconfHook
                buildPackages.pkgconfig
              ];

            buildInputs =
              [ nix
                curl
                bzip2
                xz
                pkgs.perl
                boost
              ]
              ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
              ++ lib.optional stdenv.isDarwin darwin.apple_sdk.frameworks.Security;

            configureFlags = ''
              --with-dbi=${perlPackages.DBI}/${pkgs.perl.libPrefix}
              --with-dbd-sqlite=${perlPackages.DBDSQLite}/${pkgs.perl.libPrefix}
            '';

            enableParallelBuilding = true;

            postUnpack = "sourceRoot=$sourceRoot/perl";
          };

        };

        lowdown = with final; stdenv.mkDerivation rec {
          name = "lowdown-0.8.4";

          /*
          src = fetchurl {
            url = "https://kristaps.bsd.lv/lowdown/snapshots/${name}.tar.gz";
            hash = "sha512-U9WeGoInT9vrawwa57t6u9dEdRge4/P+0wLxmQyOL9nhzOEUU2FRz2Be9H0dCjYE7p2v3vCXIYk40M+jjULATw==";
          };
          */

          src = lowdown-src;

          outputs = [ "out" "bin" "dev" ];

          nativeBuildInputs = [ buildPackages.which ];

          configurePhase = ''
              ${if (stdenv.isDarwin && stdenv.isAarch64) then "echo \"HAVE_SANDBOX_INIT=false\" > configure.local" else ""}
              ./configure \
                PREFIX=${placeholder "dev"} \
                BINDIR=${placeholder "bin"}/bin
            '';
        };

      };

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

        tests.githubFlakes = (import ./tests/github-flakes.nix rec {
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

        /*
        # Check whether we can still evaluate all of Nixpkgs.
        tests.evalNixpkgs =
          import (nixpkgs + "/pkgs/top-level/make-tarball.nix") {
            # FIXME: fix pkgs/top-level/make-tarball.nix in NixOS to not require a revCount.
            inherit nixpkgs;
            pkgs = nixpkgsFor.x86_64-linux;
            officialRelease = false;
          };

        # Check whether we can still evaluate NixOS.
        tests.evalNixOS =
          with nixpkgsFor.x86_64-linux;
          runCommand "eval-nixos" { buildInputs = [ nix ]; }
            ''
              export NIX_STATE_DIR=$TMPDIR

              nix-instantiate ${nixpkgs}/nixos/release-combined.nix -A tested --dry-run \
                --arg nixpkgs '{ outPath = ${nixpkgs}; revCount = 123; shortRev = "abcdefgh"; }'

              touch $out
            '';
        */

      };

      checks = forAllSystems (system: {
        binaryTarball = self.hydraJobs.binaryTarball.${system};
        perlBindings = self.hydraJobs.perlBindings.${system};
        installTests =
          let pkgs = nixpkgsFor.${system}; in
          pkgs.runCommand "install-tests" {
            againstSelf = testNixVersions pkgs pkgs.nix pkgs.pkgs.nix;
            againstCurrentUnstable = testNixVersions pkgs pkgs.nix pkgs.nixUnstable;
            # Disabled because the latest stable version doesn't handle
            # `NIX_DAEMON_SOCKET_PATH` which is required for the tests to work
            # againstLatestStable = testNixVersions pkgs pkgs.nix pkgs.nixStable;
          } "touch $out";
      } // (if system == "x86_64-linux" then (builtins.listToAttrs (map (crossSystem: {
        name = "binaryTarball-${crossSystem}";
        value = self.hydraJobs.binaryTarballCross.${system}.${crossSystem};
      }) crossSystems)) else {}));

      packages = forAllSystems (system: {
        inherit (nixpkgsFor.${system}) nix;
      } // (nixpkgs.lib.optionalAttrs (builtins.elem system linux64BitSystems) {
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
      }) crossSystems)));

      defaultPackage = forAllSystems (system: self.packages.${system}.nix);

      devShell = forAllSystems (system:
        with nixpkgsFor.${system};
        with commonDeps pkgs;

        stdenv.mkDerivation {
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
            '';
        });

  };
}
