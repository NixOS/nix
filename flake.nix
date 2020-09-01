{
  description = "The purely functional package manager";

  inputs.nixpkgs.url = "nixpkgs/nixos-20.03-small";
  inputs.lowdown-src = { url = "github:edolstra/lowdown/no-structs-in-anonymous-unions"; flake = false; };

  outputs = { self, nixpkgs, lowdown-src }:

    let

      version = builtins.readFile ./.version + versionSuffix;
      versionSuffix =
        if officialRelease
        then ""
        else "pre${builtins.substring 0 8 (self.lastModifiedDate or self.lastModified)}_${self.shortRev or "dirty"}";

      officialRelease = false;

      systems = [ "x86_64-linux" "i686-linux" "x86_64-darwin" "aarch64-linux" ];

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
          ];

        buildDeps =
          [ bison
            flex
            mdbook
            lowdown
            autoconf-archive
            autoreconfHook

            curl
            bzip2 xz brotli zlib editline
            openssl pkgconfig sqlite
            libarchive
            boost
            nlohmann_json

            # Tests
            git
            mercurial
            jq
            gmock
          ]
          ++ lib.optionals stdenv.isLinux [libseccomp utillinuxMinimal]
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin)
            (aws-sdk-cpp.override {
              apis = ["s3" "transfer"];
              customMemoryManagement = false;
            });

        propagatedDeps =
          [ (boehmgc.override { enableLargeConfig = true; })
          ];

        perlDeps =
          [ perl
            perlPackages.DBDSQLite
          ];
      };

    in {

      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlay = final: prev: {

        nix = with final; with commonDeps pkgs; (stdenv.mkDerivation {
          name = "nix-${version}";

          src = self;

          VERSION_SUFFIX = versionSuffix;

          outputs = [ "out" "dev" "doc" ];

          buildInputs = buildDeps;

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

          makeFlags = "profiledir=$(out)/etc/profile.d";

          doCheck = true;

          installFlags = "sysconfdir=$(out)/etc";

          postInstall = ''
            mkdir -p $doc/nix-support
            echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
          '';

          doInstallCheck = true;
          installCheckFlags = "sysconfdir=$(out)/etc";

          separateDebugInfo = true;
        }) // {

          perl-bindings = with final; stdenv.mkDerivation {
            name = "nix-perl-${version}";

            src = self;

            buildInputs =
              [ autoconf-archive
                autoreconfHook
                nix
                curl
                bzip2
                xz
                pkgconfig
                pkgs.perl
                boost
                nlohmann_json
              ]
              ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium;

            configureFlags = ''
              --with-dbi=${perlPackages.DBI}/${pkgs.perl.libPrefix}
              --with-dbd-sqlite=${perlPackages.DBDSQLite}/${pkgs.perl.libPrefix}
            '';

            enableParallelBuilding = true;

            postUnpack = "sourceRoot=$sourceRoot/perl";
          };

        };

        lowdown = with final; stdenv.mkDerivation {
          name = "lowdown-0.7.1";

          /*
          src = fetchurl {
            url = https://kristaps.bsd.lv/lowdown/snapshots/lowdown-0.7.1.tar.gz;
            hash = "sha512-1daoAQfYD0LdhK6aFhrSQvadjc5GsSPBZw0fJDb+BEHYMBLjqiUl2A7H8N+l0W4YfGKqbsPYSrCy4vct+7U6FQ==";
          };
          */

          src = lowdown-src;

          outputs = [ "out" "dev" ];

          buildInputs = [ which ];

          configurePhase =
            ''
              ./configure \
                PREFIX=${placeholder "dev"} \
                BINDIR=${placeholder "out"}/bin
            '';
        };

      };

      hydraJobs = {

        # Binary package for various platforms.
        build = nixpkgs.lib.genAttrs systems (system: nixpkgsFor.${system}.nix);

        # Perl bindings for various platforms.
        perlBindings = nixpkgs.lib.genAttrs systems (system: nixpkgsFor.${system}.nix.perl-bindings);

        # Binary tarball for various platforms, containing a Nix store
        # with the closure of 'nix' package, and the second half of
        # the installation script.
        binaryTarball = nixpkgs.lib.genAttrs systems (system:

          with nixpkgsFor.${system};

          let
            installerClosureInfo = closureInfo { rootPaths = [ nix cacert ]; };
          in

          runCommand "nix-binary-tarball-${version}"
            { #nativeBuildInputs = lib.optional (system != "aarch64-linux") shellcheck;
              meta.description = "Distribution-independent Nix bootstrap binaries for ${system}";
            }
            ''
              cp ${installerClosureInfo}/registration $TMPDIR/reginfo
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
              chmod +x $TMPDIR/install-darwin-multi-user.sh
              chmod +x $TMPDIR/install-systemd-multi-user.sh
              chmod +x $TMPDIR/install-multi-user
              dir=nix-${version}-${system}
              fn=$out/$dir.tar.xz
              mkdir -p $out/nix-support
              echo "file binary-dist $fn" >> $out/nix-support/hydra-build-products
              tar cvfJ $fn \
                --owner=0 --group=0 --mode=u+rw,uga+r \
                --absolute-names \
                --hard-dereference \
                --transform "s,$TMPDIR/install,$dir/install," \
                --transform "s,$TMPDIR/reginfo,$dir/.reginfo," \
                --transform "s,$NIX_STORE,$dir/store,S" \
                $TMPDIR/install $TMPDIR/install-darwin-multi-user.sh \
                $TMPDIR/install-systemd-multi-user.sh \
                $TMPDIR/install-multi-user $TMPDIR/reginfo \
                $(cat ${installerClosureInfo}/store-paths)
            '');

        # The first half of the installation script. This is uploaded
        # to https://nixos.org/nix/install. It downloads the binary
        # tarball for the user's system and calls the second half of the
        # installation script.
        installerScript =
          with nixpkgsFor.x86_64-linux;
          runCommand "installer-script"
            { buildInputs = [ nix ];
            }
            ''
              mkdir -p $out/nix-support

              substitute ${./scripts/install.in} $out/install \
                ${pkgs.lib.concatMapStrings
                  (system: "--replace '@binaryTarball_${system}@' $(nix --experimental-features nix-command hash-file --base16 --type sha256 ${self.hydraJobs.binaryTarball.${system}}/*.tar.xz) ")
                  [ "x86_64-linux" "i686-linux" "x86_64-darwin" "aarch64-linux" ]
                } \
                --replace '@nixVersion@' ${version}

              echo "file installer $out/install" >> $out/nix-support/hydra-build-products
            '';

        # Line coverage analysis.
        coverage =
          with nixpkgsFor.x86_64-linux;
          with commonDeps pkgs;

          releaseTools.coverageAnalysis {
            name = "nix-coverage-${version}";

            src = self;

            enableParallelBuilding = true;

            buildInputs = buildDeps ++ propagatedDeps;

            dontInstall = false;

            doInstallCheck = true;

            lcovFilter = [ "*/boost/*" "*-tab.*" ];

            # We call `dot', and even though we just use it to
            # syntax-check generated dot files, it still requires some
            # fonts.  So provide those.
            FONTCONFIG_FILE = texFunctions.fontsConf;

            # To test building without precompiled headers.
            makeFlagsArray = [ "PRECOMPILE_HEADERS=0" ];
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

        # Test whether the binary tarball works in an Ubuntu system.
        tests.binaryTarball =
          with nixpkgsFor.x86_64-linux;
          vmTools.runInLinuxImage (runCommand "nix-binary-tarball-test"
            { diskImage = vmTools.diskImages.ubuntu1204x86_64;
            }
            ''
              set -x
              useradd -m alice
              su - alice -c 'tar xf ${self.hydraJobs.binaryTarball.x86_64-linux}/*.tar.*'
              mkdir /dest-nix
              mount -o bind /dest-nix /nix # Provide a writable /nix.
              chown alice /nix
              su - alice -c '_NIX_INSTALLER_TEST=1 ./nix-*/install'
              su - alice -c 'nix-store --verify'
              su - alice -c 'PAGER= nix-store -qR ${self.hydraJobs.build.x86_64-linux}'

              # Check whether 'nix upgrade-nix' works.
              cat > /tmp/paths.nix <<EOF
              {
                x86_64-linux = "${self.hydraJobs.build.x86_64-linux}";
              }
              EOF
              su - alice -c 'nix --experimental-features nix-command upgrade-nix -vvv --nix-store-paths-url file:///tmp/paths.nix'
              (! [ -L /home/alice/.profile-1-link ])
              su - alice -c 'PAGER= nix-store -qR ${self.hydraJobs.build.x86_64-linux}'

              mkdir -p $out/nix-support
              touch $out/nix-support/hydra-build-products
              umount /nix
            '');

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
      });

      packages = forAllSystems (system: {
        inherit (nixpkgsFor.${system}) nix;
      });

      defaultPackage = forAllSystems (system: self.packages.${system}.nix);

      devShell = forAllSystems (system:
        with nixpkgsFor.${system};
        with commonDeps pkgs;

        stdenv.mkDerivation {
          name = "nix";

          outputs = [ "out" "dev" "doc" ];

          buildInputs = buildDeps ++ propagatedDeps ++ perlDeps;

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
