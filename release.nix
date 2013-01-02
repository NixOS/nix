{ nixpkgs ? <nixpkgs>
, nix ? { outPath = ./.; revCount = 1234; shortRev = "abcdef"; }
, officialRelease ? false
}:

let

  jobs = rec {


    tarball =
      with import nixpkgs {};

      releaseTools.sourceTarball {
        name = "nix-tarball";
        version = builtins.readFile ./version;
        versionSuffix = if officialRelease then "" else "pre${toString nix.revCount}_${nix.shortRev}";
        src = nix;
        inherit officialRelease;

        buildInputs =
          [ curl bison25 flex2535 perl libxml2 libxslt w3m bzip2
            tetex dblatex nukeReferences pkgconfig sqlite git
          ];

        configureFlags = ''
          --with-docbook-rng=${docbook5}/xml/rng/docbook
          --with-docbook-xsl=${docbook5_xsl}/xml/xsl/docbook
          --with-xml-flags=--nonet
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
            make dist-gzip
            make dist-xz
            mkdir -p $out/tarballs
            cp *.tar.* $out/tarballs
          '';

        preDist = ''
          make -C doc/manual install prefix=$out

          make -C doc/manual manual.pdf prefix=$out
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
          echo "doc release-notes $out/share/doc/nix/release-notes" >> $out/nix-support/hydra-build-products
        '';
      };


    build =
      { system ? "x86_64-linux" }:

      with import nixpkgs { inherit system; };

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

        installFlags = "sysconfdir=$(out)/etc";

        doInstallCheck = true;
      };

    binaryTarball =
      { system ? "x86_64-linux" }:

      with import nixpkgs { inherit system; };

      let
        toplevel = build { inherit system; };
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
            --subst-var-by nix ${toplevel} --subst-var-by regInfo /nix/store/reginfo
          chmod +x $TMPDIR/install
          fn=$out/nix-${version}-${system}.tar.bz2
          mkdir -p $out/nix-support
          echo "file binary-dist $fn" >> $out/nix-support/hydra-build-products
          tar cvfj $fn \
            --owner=0 --group=0 --absolute-names \
            --transform "s,$TMPDIR/install,/usr/bin/nix-finish-install," \
            --transform "s,$TMPDIR/reginfo,/nix/store/reginfo," \
            $TMPDIR/install $TMPDIR/reginfo $storePaths
        '';


    coverage =
      with import nixpkgs { system = "x86_64-linux"; };

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


    rpm_fedora13i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora13i386) 50;
    rpm_fedora13x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora13x86_64) 50;
    rpm_fedora16i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora16i386) 50;
    rpm_fedora16x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora16x86_64) 50;


    deb_debian60i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian60i386) 50;
    deb_debian60x86_64 = makeDeb_x86_64 (diskImageFunsFun: diskImageFunsFun.debian60x86_64) 50;

    deb_ubuntu1004i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1004i386) 50;
    deb_ubuntu1004x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1004x86_64) 50;
    deb_ubuntu1010i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1010i386) 50;
    deb_ubuntu1010x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1010x86_64) 50;
    deb_ubuntu1110i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1110i386) 60;
    deb_ubuntu1110x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1110x86_64) 60;
    deb_ubuntu1204i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1204i386) 60;
    deb_ubuntu1204x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1204x86_64) 60;
    deb_ubuntu1210i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1210i386) 70;
    deb_ubuntu1210x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1210x86_64) 70;


    # System tests.
    tests.remote_builds = (import ./tests/remote-builds.nix rec {
      nix = build { inherit system; }; system = "x86_64-linux";
    }).test;

    tests.nix_copy_closure = (import ./tests/nix-copy-closure.nix rec {
      nix = build { inherit system; }; system = "x86_64-linux";
    }).test;

  };


  makeRPM_i686 = makeRPM "i686-linux";
  makeRPM_x86_64 = makeRPM "x86_64-linux";

  makeRPM =
    system: diskImageFun: prio:

    with import nixpkgs { inherit system; };

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

    with import nixpkgs { inherit system; };

    releaseTools.debBuild {
      name = "nix-deb";
      src = jobs.tarball;
      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages = [ "libdbd-sqlite3-perl" "libsqlite3-dev" "libbz2-dev" "libwww-curl-perl" ]; };
      memSize = 1024;
      meta.schedulingPriority = prio;
      configureFlags = "--sysconfdir=/etc";
      debRequires = [ "curl" "libdbd-sqlite3-perl" "libsqlite3-0" "libbz2-1.0" "bzip2" "xz-utils" "libwww-curl-perl" ];
      doInstallCheck = true;
    };


in jobs
