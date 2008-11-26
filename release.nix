let jobs = rec {


  tarball =
    { nix ? {path = ./.; rev = 1234;}
    , nixpkgs ? {path = ../nixpkgs;}
    , officialRelease ? false
    }:
    
    with import nixpkgs.path {};
    
    releaseTools.makeSourceTarball {
      name = "nix-tarball";
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
    { tarball ? {path = jobs.tarball {};}
    , nixpkgs ? {path = ../nixpkgs;}
    , system ? "i686-linux"
    }:

    with import nixpkgs.path {inherit system;};

    releaseTools.nixBuild {
      name = "nix-build";
      src = tarball;

      buildInputs = [curl perl bzip2 openssl];

      configureFlags = ''
        --disable-init-state
        --with-bdb=${db45} --with-aterm=${aterm242fixes} --with-bzip2=${bzip2}
      '';
      
      postInstall = ''
        echo "doc manual $out/share/doc/nix/manual/manual.html" >> $out/nix-support/hydra-build-products
      '';
    };


}; in jobs
