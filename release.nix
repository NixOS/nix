{ nixpkgs ? ../nixpkgs }:

let

  jobs = rec {


    tarball =
      { nix ? {outPath = ./.; rev = 1234;}
      , officialRelease ? false
      }:

      with import nixpkgs {};

      releaseTools.sourceTarball {
        name = "nix-tarball";
        version = builtins.readFile ./version;
        src = nix;
        inherit officialRelease;

        buildInputs = [curl bison flex2533 perl libxml2 libxslt w3m bzip2 jing_tools];

        configureFlags = ''
          --with-docbook-rng=${docbook5}/xml/rng/docbook
          --with-docbook-xsl=${docbook5_xsl}/xml/xsl/docbook
          --with-xml-flags=--nonet
        '';

        # Include the BDB, ATerm and Bzip2 tarballs in the distribution.
        preConfigure = ''
          stripHash ${db45.src}
          # Remove unnecessary stuff from the Berkeley DB tarball.
          ( mkdir bdb-temp
            cd bdb-temp
            tar xfz ${db45.src}
            cd *
            rm -rf docs test tcl perl libdb_java java rpc_server build_vxworks \
              examples_java examples_c examples_cxx dist/tags
            mkdir test
            touch test/include.tcl
            cd ..
            tar cvfz ../externals/$strippedName *
          )

          stripHash ${aterm242fixes.src}
          cp -pv ${aterm242fixes.src} externals/$strippedName

          stripHash ${bzip2.src}
          cp -pv ${bzip2.src} externals/$strippedName
        '';
      };


    build =
      { tarball ? jobs.tarball {}
      , system ? "i686-linux"
      }:

      with import nixpkgs {inherit system;};

      releaseTools.nixBuild {
        name = "nix";
        src = tarball;

        buildInputs = [curl perl bzip2 openssl];

        configureFlags = ''
          --disable-init-state
          --with-bdb=${db45} --with-aterm=${aterm242fixes} --with-bzip2=${bzip2}
        '';

        postInstall = ''
          echo "doc manual $out/share/doc/nix/manual" >> $out/nix-support/hydra-build-products
          echo "doc release-notes $out/share/doc/nix/release-notes" >> $out/nix-support/hydra-build-products
        '';
      };

      
    static =
      { tarball ? jobs.tarball {}
      , system ? "i686-linux"
      }:

      with import nixpkgs {inherit system;};

      releaseTools.binaryTarball {
        name = "nix-static-tarball";
        src = tarball;

        buildInputs = [curl perl bzip2];

        configureFlags = ''
          --disable-init-state
          --disable-old-db-compat --with-aterm=${aterm242fixes} --with-bzip2=${bzip2}
          --enable-static-nix
        '';
      };

      
    coverage =
      { tarball ? jobs.tarball {}
      }:

      with import nixpkgs {};

      releaseTools.coverageAnalysis {
        name = "nix-build";
        src = tarball;

        buildInputs = [
          curl perl bzip2 openssl
          # These are for "make check" only:
          graphviz libxml2 libxslt
        ];

        configureFlags = ''
          --disable-init-state --disable-shared
          --with-bdb=${db45} --with-aterm=${aterm242fixes} --with-bzip2=${bzip2}
        '';

        lcovFilter = ["*/boost/*" "*-tab.*"];

        # We call `dot', and even though we just use it to
        # syntax-check generated dot files, it still requires some
        # fonts.  So provide those.
        FONTCONFIG_FILE = texFunctions.fontsConf;
      };

      
    rpm_fedora5i386 = makeRPM_i686 (diskImages: diskImages.fedora5i386) 20;
    rpm_fedora9i386 = makeRPM_i686 (diskImages: diskImages.fedora9i386) 50;
    rpm_fedora9x86_64 = makeRPM_x86_64 (diskImages: diskImages.fedora9x86_64) 50;
    rpm_fedora10i386 = makeRPM_i686 (diskImages: diskImages.fedora10i386) 40;
    rpm_fedora10x86_64 = makeRPM_x86_64 (diskImages: diskImages.fedora10x86_64) 40;
    rpm_opensuse103i386 = makeRPM_i686 (diskImages: diskImages.opensuse103i386) 40;

    
    deb_debian40i386 = makeDeb_i686 (diskImages: diskImages.debian40i386) 40;
    deb_debian40x86_64 = makeDeb_x86_64 (diskImages: diskImages.debian40x86_64) 40;
    deb_debian50i386 = makeDeb_i686 (diskImages: diskImages.debian50i386) 30;
    deb_debian50x86_64 = makeDeb_x86_64 (diskImages: diskImages.debian50x86_64) 30;
    deb_ubuntu804i386 = makeDeb_i686 (diskImages: diskImages.ubuntu804i386) 50;
    deb_ubuntu804x86_64 = makeDeb_x86_64 (diskImages: diskImages.ubuntu804x86_64) 50;
    deb_ubuntu810i386 = makeDeb_i686 (diskImages: diskImages.ubuntu810i386) 40;
    deb_ubuntu810x86_64 = makeDeb_x86_64 (diskImages: diskImages.ubuntu810x86_64) 40;


  };


  makeRPM_i686 = makeRPM "i686-linux";
  makeRPM_x86_64 = makeRPM "x86_64-linux";

  makeRPM = 
    system: diskImageFun: prio:
    { tarball ? jobs.tarball {}
    }:

    with import nixpkgs {inherit system;};

    releaseTools.rpmBuild rec {
      name = "nix-rpm-${diskImage.name}";
      src = tarball;
      diskImage = diskImageFun vmTools.diskImages;
      memSize = 1024;
      meta = { schedulingPriority = toString prio; };
    };


  makeDeb_i686 = makeDeb "i686-linux";
  makeDeb_x86_64 = makeDeb "x86_64-linux";
  
  makeDeb =
    system: diskImageFun: prio:
    { tarball ? jobs.tarball {}
    }:

    with import nixpkgs {inherit system;};

    releaseTools.debBuild {
      name = "nix-deb";
      src = tarball;
      diskImage = diskImageFun vmTools.diskImages;
      memSize = 1024;
      meta = { schedulingPriority = toString prio; };
    };


in jobs
