{ nix ? { outPath = ./.; revCount = 1234; shortRev = "abcdef"; }
, nixpkgs ? { outPath = <nixpkgs>; revCount = 1234; shortRev = "abcdef"; }
, officialRelease ? false
}:

let

  pkgs = import <nixpkgs> {};

  systems = [ "x86_64-linux" "i686-linux" "x86_64-darwin" /* "x86_64-freebsd" "i686-freebsd" */ ];


  jobs = rec {


    tarball =
      with pkgs;

      releaseTools.sourceTarball {
        name = "nix-tarball";
        version = builtins.readFile ./version;
        versionSuffix = if officialRelease then "" else "pre${toString nix.revCount}_${nix.shortRev}";
        src = if lib.inNixShell then null else nix;
        inherit officialRelease;

        buildInputs =
          [ curl bison flex perl libxml2 libxslt bzip2 xz
            dblatex (dblatex.tex or tetex) nukeReferences pkgconfig sqlite libsodium
            docbook5 docbook5_xsl
          ] ++ lib.optional stdenv.isLinux libseccomp
          ++ lib.optional (!lib.inNixShell) git;

        configureFlags = ''
          --with-dbi=${perlPackages.DBI}/${perl.libPrefix}
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/${perl.libPrefix}
          --with-www-curl=${perlPackages.WWWCurl}/${perl.libPrefix}
        '';

        postUnpack = ''
          # Clean up when building from a working tree.
          if [[ -d $sourceRoot/.git ]]; then
            git -C $sourceRoot clean -fd
          fi
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

          make doc/manual/manual.pdf
          cp doc/manual/manual.pdf $out/manual.pdf

          # The PDF containes filenames of included graphics (see
          # http://www.tug.org/pipermail/pdftex/2007-August/007290.html).
          # This causes a retained dependency on dblatex, which Hydra
          # doesn't like (the output of the tarball job is distributed
          # to Windows and Macs, so there should be no Linux binaries
          # in the closure).
          nuke-refs $out/manual.pdf

          echo "doc manual $out/share/doc/nix/manual" >> $out/nix-support/hydra-build-products
          echo "doc-pdf manual $out/manual.pdf" >> $out/nix-support/hydra-build-products
        '';
      };


    build = pkgs.lib.genAttrs systems (system:

      with import <nixpkgs> { inherit system; };

      releaseTools.nixBuild {
        name = "nix";
        src = tarball;

        buildInputs =
          [ curl perl bzip2 xz openssl pkgconfig sqlite boehmgc ]
          ++ lib.optional stdenv.isLinux libseccomp
          ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium;

        configureFlags = ''
          --disable-init-state
          --with-dbi=${perlPackages.DBI}/${perl.libPrefix}
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/${perl.libPrefix}
          --with-www-curl=${perlPackages.WWWCurl}/${perl.libPrefix}
          --enable-gc
          --sysconfdir=/etc
        '';

        enableParallelBuilding = true;

        makeFlags = "profiledir=$(out)/etc/profile.d";

        preBuild = "unset NIX_INDENT_MAKE";

        installFlags = "sysconfdir=$(out)/etc";

        doInstallCheck = true;
        installCheckFlags = "sysconfdir=$(out)/etc";
      });


    binaryTarball = pkgs.lib.genAttrs systems (system:

      # FIXME: temporarily use a different branch for the Darwin build.
      with import <nixpkgs> { inherit system; };

      let
        toplevel = builtins.getAttr system jobs.build;
        version = toplevel.src.version;
      in

      runCommand "nix-binary-tarball-${version}"
        { exportReferencesGraph = [ "closure1" toplevel "closure2" cacert ];
          buildInputs = [ perl shellcheck ];
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

          shellcheck -e SC1090 $TMPDIR/install
          shellcheck -e SC1091,SC2002 $TMPDIR/install-darwin-multi-user

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
      with import <nixpkgs> { system = "x86_64-linux"; };

      releaseTools.coverageAnalysis {
        name = "nix-build";
        src = tarball;

        buildInputs =
          [ curl perl bzip2 openssl pkgconfig sqlite xz libsodium libseccomp
            # These are for "make check" only:
            graphviz libxml2 libxslt
          ];

        configureFlags = ''
          --disable-init-state
          --with-dbi=${perlPackages.DBI}/${perl.libPrefix}
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/${perl.libPrefix}
          --with-www-curl=${perlPackages.WWWCurl}/${perl.libPrefix}
        '';

        dontInstall = false;

        doInstallCheck = true;

        lcovFilter = [ "*/boost/*" "*-tab.*" ];

        # We call `dot', and even though we just use it to
        # syntax-check generated dot files, it still requires some
        # fonts.  So provide those.
        FONTCONFIG_FILE = texFunctions.fontsConf;
      };


    rpm_fedora19i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora19i386) [];
    rpm_fedora19x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora19x86_64) [];
    rpm_fedora20i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora20i386) [];
    rpm_fedora20x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora20x86_64) [];
    rpm_fedora21i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora21i386) [ "libsodium-devel" ];
    rpm_fedora21x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora21x86_64) [ "libsodium-devel" ];


    deb_debian8i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian8i386) [ "libsodium-dev" ] [ "libsodium13" ];
    deb_debian8x86_64 = makeDeb_x86_64 (diskImageFunsFun: diskImageFunsFun.debian8x86_64) [ "libsodium-dev" ] [ "libsodium13" ];

    deb_ubuntu1404i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1404i386) [] [];
    deb_ubuntu1404x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1404x86_64) [] [];
    deb_ubuntu1410i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1410i386) [] [];
    deb_ubuntu1410x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1410x86_64) [] [];
    deb_ubuntu1504i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1504i386) [ "libsodium-dev" ] [ "libsodium13" ];
    deb_ubuntu1504x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1504x86_64) [ "libsodium-dev" ] [ "libsodium13" ];
    deb_ubuntu1510i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1510i386) [ "libsodium-dev" ] [ "libsodium13"];
    deb_ubuntu1510x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1510x86_64) [ "libsodium-dev" ] [ "libsodium13" ];
    deb_ubuntu1604i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1604i386) [ "libsodium-dev" ] [ "libsodium18" ];
    deb_ubuntu1604x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1604x86_64) [ "libsodium-dev" ] [ "libsodium18" ];


    # System tests.
    tests.remoteBuilds = (import ./tests/remote-builds.nix rec {
      nix = build.x86_64-linux; system = "x86_64-linux";
    });

    tests.nix-copy-closure = (import ./tests/nix-copy-closure.nix rec {
      nix = build.x86_64-linux; system = "x86_64-linux";
    });

    tests.setuid = pkgs.lib.genAttrs (pkgs.lib.filter (pkgs.lib.hasSuffix "-linux") systems) (system:
      import ./tests/setuid.nix rec {
        nix = build.${system}; inherit system;
      });

    tests.binaryTarball =
      with import <nixpkgs> { system = "x86_64-linux"; };
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
      import <nixpkgs/pkgs/top-level/make-tarball.nix> {
        inherit nixpkgs;
        inherit pkgs;
        nix = build.x86_64-linux;
        officialRelease = false;
      };

    tests.evalNixOS =
      pkgs.runCommand "eval-nixos" { buildInputs = [ build.x86_64-linux ]; }
        ''
          export NIX_DB_DIR=$TMPDIR
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
          #build.i686-freebsd
          build.i686-linux
          build.x86_64-darwin
          #build.x86_64-freebsd
          build.x86_64-linux
          #binaryTarball.i686-freebsd
          binaryTarball.i686-linux
          binaryTarball.x86_64-darwin
          #binaryTarball.x86_64-freebsd
          binaryTarball.x86_64-linux
          deb_debian8i386
          deb_debian8x86_64
          deb_ubuntu1404i386 # LTS
          deb_ubuntu1404x86_64 # LTS
          deb_ubuntu1504i386
          deb_ubuntu1504x86_64
          rpm_fedora20i386
          rpm_fedora20x86_64
          rpm_fedora21i386
          rpm_fedora21x86_64
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

    with import <nixpkgs> { inherit system; };

    releaseTools.rpmBuild rec {
      name = "nix-rpm";
      src = jobs.tarball;
      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages =
            [ "perl-DBD-SQLite" "perl-devel" "sqlite" "sqlite-devel" "bzip2-devel" "emacs" "perl-WWW-Curl" "libcurl-devel" "openssl-devel" "xz-devel" "libseccomp-devel" ]
            ++ extraPackages; };
      memSize = 1024;
      meta.schedulingPriority = 50;
      postRPMInstall = "cd /tmp/rpmout/BUILD/nix-* && make installcheck";
    };


  makeDeb_i686 = makeDeb "i686-linux";
  makeDeb_x86_64 = makeDeb "x86_64-linux";

  makeDeb =
    system: diskImageFun: extraPackages: extraDebPackages:

    with import <nixpkgs> { inherit system; };

    releaseTools.debBuild {
      name = "nix-deb";
      src = jobs.tarball;
      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages =
            [ "libdbd-sqlite3-perl" "libsqlite3-dev" "libbz2-dev" "libwww-curl-perl" "libcurl-dev" "libcurl3-nss" "libssl-dev" "liblzma-dev" "libseccomp-dev" ]
            ++ extraPackages; };
      memSize = 1024;
      meta.schedulingPriority = 50;
      postInstall = "make installcheck";
      configureFlags = "--sysconfdir=/etc";
      debRequires =
        [ "curl" "libdbd-sqlite3-perl" "libsqlite3-0" "libbz2-1.0" "bzip2" "xz-utils" "libwww-curl-perl" "libssl1.0.0" "liblzma5" "libseccomp2" ]
        ++ extraDebPackages;
      debMaintainer = "Eelco Dolstra <eelco.dolstra@logicblox.com>";
      doInstallCheck = true;
    };


in jobs
