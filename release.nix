{ nix ? builtins.fetchGit ./.
, nixpkgs ? builtins.fetchTarball https://github.com/NixOS/nixpkgs/archive/nixos-20.03-small.tar.gz
, officialRelease ? false
, systems ? [ "x86_64-linux" "i686-linux" "x86_64-darwin" "aarch64-linux" ]
}:

let

  pkgs = import nixpkgs { system = builtins.currentSystem or "x86_64-linux"; };

  version =
    builtins.readFile ./.version
    + (if officialRelease then "" else "pre${toString nix.revCount}_${nix.shortRev}");

  # Create a "vendor" directory that contains the crates listed in
  # Cargo.lock. This allows Nix to be built without network access.
  vendoredCrates' =
    let
      lockFile = builtins.fromTOML (builtins.readFile nix-rust/Cargo.lock);

      files = map (pkg: import <nix/fetchurl.nix> {
        url = "https://crates.io/api/v1/crates/${pkg.name}/${pkg.version}/download";
        sha256 = lockFile.metadata."checksum ${pkg.name} ${pkg.version} (registry+https://github.com/rust-lang/crates.io-index)";
      }) (builtins.filter (pkg: pkg.source or "" == "registry+https://github.com/rust-lang/crates.io-index") lockFile.package);

    in pkgs.runCommand "cargo-vendor-dir" {}
      ''
        mkdir -p $out/vendor

        cat > $out/vendor/config <<EOF
        [source.crates-io]
        replace-with = "vendored-sources"

        [source.vendored-sources]
        directory = "vendor"
        EOF

        ${toString (builtins.map (file: ''
          mkdir $out/vendor/tmp
          tar xvf ${file} -C $out/vendor/tmp
          dir=$(echo $out/vendor/tmp/*)

          # Add just enough metadata to keep Cargo happy.
          printf '{"files":{},"package":"${file.outputHash}"}' > "$dir/.cargo-checksum.json"

          # Clean up some cruft from the winapi crates. FIXME: find
          # a way to remove winapi* from our dependencies.
          if [[ $dir =~ /winapi ]]; then
            find $dir -name "*.a" -print0 | xargs -0 rm -f --
          fi

          mv "$dir" $out/vendor/

          rm -rf $out/vendor/tmp
        '') files)}
      '';

  jobs = rec {

    vendoredCrates =
      with pkgs;
      runCommand "vendored-crates" {}
        ''
          mkdir -p $out/nix-support
          name=nix-vendored-crates-${version}
          fn=$out/$name.tar.xz
            tar cvfJ $fn -C ${vendoredCrates'} vendor \
              --owner=0 --group=0 --mode=u+rw,uga+r \
              --transform "s,vendor,$name,"
            echo "file crates-tarball $fn" >> $out/nix-support/hydra-build-products
        '';

    build = pkgs.lib.genAttrs systems (system:

      let pkgs = import nixpkgs { inherit system; }; in

      with pkgs;

      with import ./release-common.nix { inherit pkgs; };

      stdenv.mkDerivation {
        name = "nix-${version}";

        src = nix;

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

            ln -sfn ${vendoredCrates'}/vendor/ nix-rust/vendor

            (cd perl; autoreconf --install --force --verbose)
          '';

        configureFlags = configureFlags ++
          [ "--sysconfdir=/etc" ];

        enableParallelBuilding = true;

        makeFlags = "profiledir=$(out)/etc/profile.d";

        installFlags = "sysconfdir=$(out)/etc";

        postInstall = ''
          mkdir -p $doc/nix-support
          echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
        '';

        doCheck = true;

        doInstallCheck = true;
        installCheckFlags = "sysconfdir=$(out)/etc";

        separateDebugInfo = true;
      });


    perlBindings = pkgs.lib.genAttrs systems (system:

      let pkgs = import nixpkgs { inherit system; }; in with pkgs;

      releaseTools.nixBuild {
        name = "nix-perl-${version}";

        src = nix;

        buildInputs =
          [ autoconf-archive
            autoreconfHook
            jobs.build.${system}
            curl
            bzip2
            xz
            pkgconfig
            pkgs.perl
            boost
          ]
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium;

        configureFlags = ''
          --with-dbi=${perlPackages.DBI}/${pkgs.perl.libPrefix}
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/${pkgs.perl.libPrefix}
        '';

        enableParallelBuilding = true;

        postUnpack = "sourceRoot=$sourceRoot/perl";
      });


    binaryTarball = pkgs.lib.genAttrs systems (system:

      with import nixpkgs { inherit system; };

      let
        toplevel = builtins.getAttr system jobs.build;
        installerClosureInfo = closureInfo { rootPaths = [ toplevel cacert ]; };
      in

      runCommand "nix-binary-tarball-${version}"
        { #nativeBuildInputs = lib.optional (system != "aarch64-linux") shellcheck;
          meta.description = "Distribution-independent Nix bootstrap binaries for ${system}";
        }
        ''
          cp ${installerClosureInfo}/registration $TMPDIR/reginfo
          cp ${./scripts/create-darwin-volume.sh} $TMPDIR/create-darwin-volume.sh
          substitute ${./scripts/install-nix-from-closure.sh} $TMPDIR/install \
            --subst-var-by nix ${toplevel} \
            --subst-var-by cacert ${cacert}
          substitute ${./scripts/install-darwin-multi-user.sh} $TMPDIR/install-darwin-multi-user.sh \
            --subst-var-by nix ${toplevel} \
            --subst-var-by cacert ${cacert}
          substitute ${./scripts/install-systemd-multi-user.sh} $TMPDIR/install-systemd-multi-user.sh \
            --subst-var-by nix ${toplevel} \
            --subst-var-by cacert ${cacert}
          substitute ${./scripts/install-multi-user.sh} $TMPDIR/install-multi-user \
            --subst-var-by nix ${toplevel} \
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
          dir=nix-${version}-${system}
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
        '');


    coverage =
      with pkgs;

      with import ./release-common.nix { inherit pkgs; };

      releaseTools.coverageAnalysis {
        name = "nix-coverage-${version}";

        src = nix;

        preConfigure =
          ''
            ln -sfn ${vendoredCrates'}/vendor/ nix-rust/vendor
          '';

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
    tests.remoteBuilds = (import ./tests/remote-builds.nix rec {
      inherit nixpkgs;
      nix = build.x86_64-linux; system = "x86_64-linux";
    });

    tests.nix-copy-closure = (import ./tests/nix-copy-closure.nix rec {
      inherit nixpkgs;
      nix = build.x86_64-linux; system = "x86_64-linux";
    });

    tests.setuid = pkgs.lib.genAttrs
      ["i686-linux" "x86_64-linux"]
      (system:
        import ./tests/setuid.nix rec {
          inherit nixpkgs;
          nix = build.${system}; inherit system;
        });

    tests.binaryTarball =
      with import nixpkgs { system = "x86_64-linux"; };
      vmTools.runInLinuxImage (runCommand "nix-binary-tarball-test"
        { diskImage = vmTools.diskImages.ubuntu1204x86_64;
        }
        ''
          set -x
          useradd -m alice
          su - alice -c 'tar xf ${binaryTarball.x86_64-linux}/*.tar.*'
          mkdir /dest-nix
          mount -o bind /dest-nix /nix # Provide a writable /nix.
          chown alice /nix
          su - alice -c '_NIX_INSTALLER_TEST=1 ./nix-*/install'
          su - alice -c 'nix-store --verify'
          su - alice -c 'PAGER= nix-store -qR ${build.x86_64-linux}'

          # Check whether 'nix upgrade-nix' works.
          cat > /tmp/paths.nix <<EOF
          {
            x86_64-linux = "${build.x86_64-linux}";
          }
          EOF
          su - alice -c 'nix --experimental-features nix-command upgrade-nix -vvv --nix-store-paths-url file:///tmp/paths.nix'
          (! [ -L /home/alice/.profile-1-link ])
          su - alice -c 'PAGER= nix-store -qR ${build.x86_64-linux}'

          mkdir -p $out/nix-support
          touch $out/nix-support/hydra-build-products
          umount /nix
        ''); # */

    /*
    tests.evalNixpkgs =
      import (nixpkgs + "/pkgs/top-level/make-tarball.nix") {
        inherit nixpkgs;
        inherit pkgs;
        nix = build.x86_64-linux;
        officialRelease = false;
      };

    tests.evalNixOS =
      pkgs.runCommand "eval-nixos" { buildInputs = [ build.x86_64-linux ]; }
        ''
          export NIX_STATE_DIR=$TMPDIR

          nix-instantiate ${nixpkgs}/nixos/release-combined.nix -A tested --dry-run \
            --arg nixpkgs '{ outPath = ${nixpkgs}; revCount = 123; shortRev = "abcdefgh"; }'

          touch $out
        '';
    */


    installerScript =
      pkgs.runCommand "installer-script"
        { buildInputs = [ build.${builtins.currentSystem or "x86_64-linux"} ]; }
        ''
          mkdir -p $out/nix-support

          substitute ${./scripts/install.in} $out/install \
            ${pkgs.lib.concatMapStrings
              (system: "--replace '@binaryTarball_${system}@' $(nix --experimental-features nix-command hash-file --base16 --type sha256 ${binaryTarball.${system}}/*.tar.xz) ")
              systems
            } \
            --replace '@nixVersion@' ${version}

          echo "file installer $out/install" >> $out/nix-support/hydra-build-products
        '';

  };


in jobs
