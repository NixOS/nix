{ nix ? builtins.fetchGit ./.
, nixpkgs ? builtins.fetchGit { url = https://github.com/NixOS/nixpkgs.git; ref = "nix-2.0"; }
, officialRelease ? false
, systems ? [ "x86_64-linux" "i686-linux" "x86_64-darwin" "aarch64-linux" ]
}:

let

  pkgs = import nixpkgs { system = builtins.currentSystem or "x86_64-linux"; };

  jobs = rec {


    tarball =
      with pkgs;

      releaseTools.sourceTarball {
        name = "nix-tarball";
        version = builtins.readFile ./version;
        versionSuffix = if officialRelease then "" else "pre${toString nix.revCount}.${nix.shortRev}";
        src = nix;
        inherit officialRelease;

        buildInputs =
          [ curl bison flex libxml2 libxslt
            bzip2 xz brotli
            pkgconfig sqlite libsodium boehmgc
            docbook5 docbook5_xsl
            autoconf-archive
          ] ++ lib.optional stdenv.isLinux libseccomp;

        configureFlags = "--enable-gc";

        postUnpack = ''
          (cd source && find . -type f) | cut -c3- > source/.dist-files
          cat source/.dist-files
        '';

        preConfigure = ''
          # TeX needs a writable font cache.
          export VARTEXFONTS=$TMPDIR/texfonts
        '';

        distPhase =
          ''
            runHook preDist
            make dist
            mkdir -p $out/tarballs
            cp *.tar.* $out/tarballs
          '';

        preDist = ''
          make install docdir=$out/share/doc/nix makefiles=doc/manual/local.mk
          echo "doc manual $out/share/doc/nix/manual" >> $out/nix-support/hydra-build-products
        '';
      };


    build = pkgs.lib.genAttrs systems (system:

      with import nixpkgs { inherit system; };

      with import ./release-common.nix { inherit pkgs; };

      releaseTools.nixBuild {
        name = "nix";
        src = tarball;

        buildInputs =
          [ curl
            bzip2 xz brotli
            openssl pkgconfig sqlite boehmgc

            # Tests
            git
            mercurial
          ]
          ++ lib.optional stdenv.isLinux libseccomp
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin)
            (aws-sdk-cpp.override {
              apis = ["s3"];
              customMemoryManagement = false;
            });

        configureFlags = configureFlags ++
          [ "--sysconfdir=/etc" ];

        enableParallelBuilding = true;

        makeFlags = "profiledir=$(out)/etc/profile.d";

        preBuild = "unset NIX_INDENT_MAKE";

        installFlags = "sysconfdir=$(out)/etc";

        doInstallCheck = true;
        installCheckFlags = "sysconfdir=$(out)/etc";
      });


    perlBindings = pkgs.lib.genAttrs systems (system:

      let pkgs = import nixpkgs { inherit system; }; in with pkgs;

      releaseTools.nixBuild {
        name = "nix-perl";
        src = tarball;

        buildInputs =
          [ (builtins.getAttr system jobs.build) curl bzip2 xz pkgconfig pkgs.perl ]
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium;

        configureFlags = ''
          --with-dbi=${perlPackages.DBI}/${pkgs.perl.libPrefix}
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/${pkgs.perl.libPrefix}
        '';

        enableParallelBuilding = true;

        postUnpack = "sourceRoot=$sourceRoot/perl";

        preBuild = "unset NIX_INDENT_MAKE";
      });


    binaryTarball = pkgs.lib.genAttrs systems (system:

      with import nixpkgs { inherit system; };

      let
        toplevel = builtins.getAttr system jobs.build;
        version = toplevel.src.version;
      in

      runCommand "nix-binary-tarball-${version}"
        { exportReferencesGraph = [ "closure1" toplevel "closure2" cacert ];
          buildInputs = [ perl ] ++ lib.optional (system != "aarch64-linux") shellcheck;
          meta.description = "Distribution-independent Nix bootstrap binaries for ${system}";
        }
        ''
          storePaths=$(perl ${pathsFromGraph} ./closure1 ./closure2)
          printRegistration=1 perl ${pathsFromGraph} ./closure1 ./closure2 > $TMPDIR/reginfo
          substitute ${./scripts/install-nix-from-closure.sh} $TMPDIR/install \
            --subst-var-by nix ${toplevel} \
            --subst-var-by cacert ${cacert}
          substitute ${./scripts/install-darwin-multi-user.sh} $TMPDIR/install-darwin-multi-user \
            --subst-var-by nix ${toplevel} \
            --subst-var-by cacert ${cacert}

          if type -p shellcheck; then
            shellcheck -e SC1090 $TMPDIR/install
            shellcheck -e SC1091,SC2002 $TMPDIR/install-darwin-multi-user
          fi

          chmod +x $TMPDIR/install
          chmod +x $TMPDIR/install-darwin-multi-user
          dir=nix-${version}-${system}
          fn=$out/$dir.tar.bz2
          mkdir -p $out/nix-support
          echo "file binary-dist $fn" >> $out/nix-support/hydra-build-products
          tar cvfj $fn \
            --owner=0 --group=0 --mode=u+rw,uga+r \
            --absolute-names \
            --hard-dereference \
            --transform "s,$TMPDIR/install,$dir/install," \
            --transform "s,$TMPDIR/reginfo,$dir/.reginfo," \
            --transform "s,$NIX_STORE,$dir/store,S" \
            $TMPDIR/install $TMPDIR/install-darwin-multi-user $TMPDIR/reginfo $storePaths
        '');


    coverage =
      with import nixpkgs { system = "x86_64-linux"; };

      releaseTools.coverageAnalysis {
        name = "nix-build";
        src = tarball;

        buildInputs =
          [ curl bzip2 openssl pkgconfig sqlite xz libsodium libseccomp
            # These are for "make check" only:
            graphviz libxml2 libxslt git mercurial
          ];

        configureFlags = ''
          --disable-init-state
        '';

        dontInstall = false;

        doInstallCheck = true;

        lcovFilter = [ "*/boost/*" "*-tab.*" "*/nlohmann/*" "*/linenoise/*" ];

        # We call `dot', and even though we just use it to
        # syntax-check generated dot files, it still requires some
        # fonts.  So provide those.
        FONTCONFIG_FILE = texFunctions.fontsConf;
      };


    rpm_fedora27x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora27x86_64) [ "brotli-devel" ];

    deb_debian9i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian9i386) [ ];
    deb_debian9x86_64 = makeDeb_x86_64 (diskImageFunsFun: diskImageFunsFun.debian9x86_64) [ ];

    deb_ubuntu1604i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1604i386) [ ];
    deb_ubuntu1604x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1604x86_64) [ ];

    deb_ubuntu1710i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1710i386) [ ];
    deb_ubuntu1710x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1710x86_64) [ ];

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
          useradd -m alice
          su - alice -c 'tar xf ${binaryTarball.x86_64-linux}/*.tar.*'
          mkdir /dest-nix
          mount -o bind /dest-nix /nix # Provide a writable /nix.
          chown alice /nix
          su - alice -c '_NIX_INSTALLER_TEST=1 ./nix-*/install'
          su - alice -c 'nix-store --verify'
          su - alice -c 'PAGER= nix-store -qR ${build.x86_64-linux}'
          mkdir -p $out/nix-support
          touch $out/nix-support/hydra-build-products
          umount /nix
        ''); # */

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
          nix-store --init

          nix-instantiate ${nixpkgs}/nixos/release-combined.nix -A tested --dry-run

          touch $out
        '';


    # Aggregate job containing the release-critical jobs.
    release = pkgs.releaseTools.aggregate {
      name = "nix-${tarball.version}";
      meta.description = "Release-critical builds";
      constituents =
        [ tarball
          build.i686-linux
          build.x86_64-darwin
          build.x86_64-linux
          binaryTarball.i686-linux
          binaryTarball.x86_64-darwin
          binaryTarball.x86_64-linux
          deb_debian9i386
          deb_debian9x86_64
          deb_ubuntu1710i386
          deb_ubuntu1710x86_64
          rpm_fedora27x86_64
          tests.remoteBuilds
          tests.nix-copy-closure
          tests.binaryTarball
          tests.evalNixpkgs
          tests.evalNixOS
        ];
    };

  };


  makeRPM_i686 = makeRPM "i686-linux";
  makeRPM_x86_64 = makeRPM "x86_64-linux";

  makeRPM =
    system: diskImageFun: extraPackages:

    with import nixpkgs { inherit system; };

    releaseTools.rpmBuild rec {
      name = "nix-rpm";
      src = jobs.tarball;

      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages =
            [ "sqlite" "sqlite-devel" "bzip2-devel" "libcurl-devel" "openssl"
              "openssl-devel" "xz-devel" "libseccomp-devel" "libsodium-devel" ]
            ++ extraPackages; };
      # At most 2047MB can be simulated in qemu-system-i386
      memSize = 2047;
      meta.schedulingPriority = 50;

      postRPMInstall = ''
        cd /tmp/rpmout/BUILD/nix-*
        make installcheck
        useradd alice
        mount -t tmpfs none /nix/store
        nix-daemon &
        su -c 'bash -l -c "nix-build --option binary-caches \"\" --no-out-link tests/dependencies.nix"' alice
        umount /nix/store
      '';
    };


  makeDeb_i686 = makeDeb "i686-linux";
  makeDeb_x86_64 = makeDeb "x86_64-linux";

  makeDeb =
    system: diskImageFun: extraPackages:

    with import nixpkgs { inherit system; };

    releaseTools.debBuild {
      name = "nix-deb";
      src = jobs.tarball;

      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages =
            [ "build-essential" "debhelper" "mount"
              "libsqlite3-dev" "libbz2-dev" "libcurl-dev" "libsodium-dev"
              "libcurl4-openssl-dev" "libssl-dev" "liblzma-dev" "libseccomp-dev" ]
            ++ extraPackages; };
      memSize = 1024;
      meta.schedulingPriority = 50;

      configurePhase = "true";

      buildPhase = ''
        dpkg-buildpackage -us -uc
      '';

      installPhase = ''
        mkdir -p $out/debs
        cp -r ../*.deb $out/debs
      '';

      postDebInstall = ''
        make installcheck
        useradd alice
        mount -t tmpfs none /nix/store
        rm -rf /homeless-shelter
        nix-daemon &
        su -c 'bash -l -c "nix-build --option binary-caches \"\" --no-out-link tests/dependencies.nix"' alice
        umount /nix/store
      '';

      debMaintainer = "Eelco Dolstra <eelco.dolstra@logicblox.com>";
    };


in jobs
