{ nix ? { outPath = ./.; revCount = 1234; shortRev = "abcdef"; }
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
        src = nix;
        inherit officialRelease;

        buildInputs =
          [ curl bison flex perl libxml2 libxslt w3m bzip2
            tetex dblatex nukeReferences pkgconfig sqlite git
          ];

        configureFlags = ''
          --with-docbook-rng=${docbook5}/xml/rng/docbook
          --with-docbook-xsl=${docbook5_xsl}/xml/xsl/docbook
          --with-dbi=${perlPackages.DBI}/${perl.libPrefix}
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/${perl.libPrefix}
          --with-www-curl=${perlPackages.WWWCurl}/${perl.libPrefix}
        '';

        postUnpack = ''
          # Clean up when building from a working tree.
          (cd $sourceRoot && (git ls-files -o | xargs -r rm -v))
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
          echo "doc release-notes $out/share/doc/nix/manual release-notes.html" >> $out/nix-support/hydra-build-products
        '';
      };


    build = pkgs.lib.genAttrs systems (system:

      with import <nixpkgs> { inherit system; };

      releaseTools.nixBuild {
        name = "nix";
        src = tarball;

        buildInputs = [ curl perl bzip2 openssl pkgconfig sqlite boehmgc ];

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

      with import <nixpkgs> { inherit system; };

      let
        toplevel = builtins.getAttr system jobs.build;
        version = toplevel.src.version;
      in

      runCommand "nix-binary-tarball-${version}"
        { exportReferencesGraph = [ "closure" toplevel ];
          buildInputs = [ perl ];
          meta.description = "Distribution-independent Nix bootstrap binaries for ${system}";
        }
        ''
          storePaths=$(perl ${pathsFromGraph} ./closure)
          printRegistration=1 perl ${pathsFromGraph} ./closure > $TMPDIR/reginfo
          substitute ${./scripts/install-nix-from-closure.sh} $TMPDIR/install \
            --subst-var-by nix ${toplevel}
          chmod +x $TMPDIR/install
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
            $TMPDIR/install $TMPDIR/reginfo $storePaths
        '');


    coverage =
      with import <nixpkgs> { system = "x86_64-linux"; };

      releaseTools.coverageAnalysis {
        name = "nix-build";
        src = tarball;

        buildInputs =
          [ curl perl bzip2 openssl pkgconfig sqlite
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


    rpm_fedora16i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora16i386) 50;
    rpm_fedora16x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora16x86_64) 50;
    rpm_fedora18i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora18i386) 60;
    rpm_fedora18x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora18x86_64) 60;
    rpm_fedora19i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora19i386) 70;
    rpm_fedora19x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora19x86_64) 70;
    rpm_fedora20i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora20i386) 70;
    rpm_fedora20x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora20x86_64) 70;


    #deb_debian60i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian60i386) 50;
    #deb_debian60x86_64 = makeDeb_x86_64 (diskImageFunsFun: diskImageFunsFun.debian60x86_64) 50;
    deb_debian7i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian7i386) 60;
    deb_debian7x86_64 = makeDeb_x86_64 (diskImageFunsFun: diskImageFunsFun.debian7x86_64) 60;

    deb_ubuntu1110i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1110i386) 60;
    deb_ubuntu1110x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1110x86_64) 60;
    deb_ubuntu1204i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1204i386) 60;
    deb_ubuntu1204x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1204x86_64) 60;
    deb_ubuntu1210i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1210i386) 70;
    deb_ubuntu1210x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1210x86_64) 70;
    deb_ubuntu1304i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1304i386) 80;
    deb_ubuntu1304x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1304x86_64) 80;
    deb_ubuntu1310i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1310i386) 90;
    deb_ubuntu1310x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1310x86_64) 90;
    deb_ubuntu1404i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1404i386) 90;
    deb_ubuntu1404x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1404x86_64) 90;


    # System tests.
    tests.remote_builds = (import ./tests/remote-builds.nix rec {
      nix = build.x86_64-linux; system = "x86_64-linux";
    }).test;

    tests.nix_copy_closure = (import ./tests/nix-copy-closure.nix rec {
      nix = build.x86_64-linux; system = "x86_64-linux";
    }).test;


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
          deb_debian7i386
          deb_debian7x86_64
          deb_ubuntu1304i386
          deb_ubuntu1304x86_64
          deb_ubuntu1310i386
          deb_ubuntu1310x86_64
          deb_ubuntu1404i386
          deb_ubuntu1404x86_64
          rpm_fedora19i386
          rpm_fedora19x86_64
          rpm_fedora20i386
          rpm_fedora20x86_64
          tests.remote_builds
          tests.nix_copy_closure
        ];
    };

  };


  makeRPM_i686 = makeRPM "i686-linux";
  makeRPM_x86_64 = makeRPM "x86_64-linux";

  makeRPM =
    system: diskImageFun: prio:

    with import <nixpkgs> { inherit system; };

    releaseTools.rpmBuild rec {
      name = "nix-rpm";
      src = jobs.tarball;
      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages = [ "perl-DBD-SQLite" "perl-devel" "sqlite" "sqlite-devel" "bzip2-devel" "emacs" "perl-WWW-Curl" ]; };
      memSize = 1024;
      meta.schedulingPriority = prio;
      postRPMInstall = "cd /tmp/rpmout/BUILD/nix-* && make installcheck";
    };


  makeDeb_i686 = makeDeb "i686-linux";
  makeDeb_x86_64 = makeDeb "x86_64-linux";

  makeDeb =
    system: diskImageFun: prio:

    with import <nixpkgs> { inherit system; };

    releaseTools.debBuild {
      name = "nix-deb";
      src = jobs.tarball;
      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages = [ "libdbd-sqlite3-perl" "libsqlite3-dev" "libbz2-dev" "libwww-curl-perl" ]; };
      memSize = 1024;
      meta.schedulingPriority = prio;
      configureFlags = "--sysconfdir=/etc";
      debRequires = [ "curl" "libdbd-sqlite3-perl" "libsqlite3-0" "libbz2-1.0" "bzip2" "xz-utils" "libwww-curl-perl" ];
      debMaintainer = "Eelco Dolstra <eelco.dolstra@logicblox.com>";
      doInstallCheck = true;
    };


in jobs
