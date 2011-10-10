{ nixpkgs ? ../nixpkgs
, nix ? { outPath = ./.; rev = 1234; }
, officialRelease ? false
}:

let

  jobs = rec {


    tarball =
      with import nixpkgs {};

      releaseTools.sourceTarball {
        name = "nix-tarball";
        version = builtins.readFile ./version;
        src = nix;
        inherit officialRelease;

        buildInputs =
          [ curl bison24 flex2535 perl libxml2 libxslt w3m bzip2
            tetex dblatex nukeReferences pkgconfig
          ];

        configureFlags = ''
          --with-docbook-rng=${docbook5}/xml/rng/docbook
          --with-docbook-xsl=${docbook5_xsl}/xml/xsl/docbook
          --with-xml-flags=--nonet
          --with-dbi=${perlPackages.DBI}/lib/perl5/site_perl
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/lib/perl5/site_perl
        '';

        # Include the Bzip2 tarball in the distribution.
        preConfigure = ''
          stripHash ${bzip2.src}
          cp -pv ${bzip2.src} externals/$strippedName

          stripHash ${sqlite.src}
          cp -pv ${sqlite.src} externals/$strippedName

          # TeX needs a writable font cache.
          export VARTEXFONTS=$TMPDIR/texfonts
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
      { system ? "i686-linux" }:

      with import nixpkgs { inherit system; };

      releaseTools.nixBuild {
        name = "nix";
        src = tarball;

        buildInputs = [ curl perl bzip2 openssl pkgconfig boehmgc ];

        configureFlags = ''
          --disable-init-state
          --with-bzip2=${bzip2} --with-sqlite=${sqlite}
          --with-dbi=${perlPackages.DBI}/lib/perl5/site_perl
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/lib/perl5/site_perl
          --enable-gc
        '';
      };


    coverage =
      with import nixpkgs { system = "x86_64-linux"; };

      releaseTools.coverageAnalysis {
        name = "nix-build";
        src = tarball;

        buildInputs =
          [ curl perl bzip2 openssl
            # These are for "make check" only:
            graphviz libxml2 libxslt
          ];

        configureFlags = ''
          --disable-init-state --disable-shared
          --with-bzip2=${bzip2} --with-sqlite=${sqlite}
          --with-dbi=${perlPackages.DBI}/lib/perl5/site_perl
          --with-dbd-sqlite=${perlPackages.DBDSQLite}/lib/perl5/site_perl
        '';

        lcovFilter = [ "*/boost/*" "*-tab.*" ];

        # We call `dot', and even though we just use it to
        # syntax-check generated dot files, it still requires some
        # fonts.  So provide those.
        FONTCONFIG_FILE = texFunctions.fontsConf;
      };

      
    rpm_fedora10i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora10i386) 20;
    rpm_fedora10x86_64 = makeRPM_x86_64 (diskImageFuns: diskImageFuns.fedora10x86_64) 20;
    rpm_fedora11i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora11i386) 30;
    rpm_fedora11x86_64 = makeRPM_x86_64 (diskImageFuns: diskImageFuns.fedora11x86_64) 30;
    rpm_fedora12i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora12i386) 40;
    rpm_fedora12x86_64 = makeRPM_x86_64 (diskImageFuns: diskImageFuns.fedora12x86_64) 40;
    rpm_fedora13i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.fedora13i386) 50;
    rpm_fedora13x86_64 = makeRPM_x86_64 (diskImageFunsFun: diskImageFunsFun.fedora13x86_64) 50;
    rpm_opensuse103i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.opensuse103i386) 40;
    rpm_opensuse110i386 = makeRPM_i686 (diskImageFuns: diskImageFuns.opensuse110i386) 50;
    rpm_opensuse110x86_64 = makeRPM_x86_64 (diskImageFuns: diskImageFuns.opensuse110x86_64) 50;

    
    deb_debian50i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian50i386) 50;
    deb_debian50x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.debian50x86_64) 50;
    deb_debian60i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.debian60i386) 50;
    deb_debian60x86_64 = makeDeb_x86_64 (diskImageFunsFun: diskImageFunsFun.debian60x86_64) 50;
    deb_ubuntu904i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu904i386) 40;
    deb_ubuntu904x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu904x86_64) 40;
    deb_ubuntu910i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu910i386) 50;
    deb_ubuntu910x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu910x86_64) 50;
    deb_ubuntu1004i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1004i386) 50;
    deb_ubuntu1004x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1004x86_64) 50;
    deb_ubuntu1010i386 = makeDeb_i686 (diskImageFuns: diskImageFuns.ubuntu1010i386) 50;
    deb_ubuntu1010x86_64 = makeDeb_x86_64 (diskImageFuns: diskImageFuns.ubuntu1010x86_64) 50;


  };


  makeRPM_i686 = makeRPM "i686-linux";
  makeRPM_x86_64 = makeRPM "x86_64-linux";

  makeRPM = 
    system: diskImageFun: prio:

    with import nixpkgs { inherit system; };

    releaseTools.rpmBuild rec {
      name = "nix-rpm-${diskImage.name}";
      src = jobs.tarball;
      diskImage = (diskImageFun vmTools.diskImageFuns)
        { extraPackages = [ "perl-DBD-SQLite" "perl-devel" ]; };
      memSize = 1024;
      meta.schedulingPriority = prio;
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
        { extraPackages = [ "libdbd-sqlite3-perl" ]; };
      memSize = 1024;
      meta.schedulingPriority = prio;
      configureFlags = "--sysconfdir=/etc";
      debRequires = [ "curl" "libdbd-sqlite3-perl" ];
    };


in jobs
