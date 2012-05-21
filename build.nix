with import <nix-make/lib>;
with pkgs;

rec {

  bin2c = link {
    objects = [ (compileC { main = ./src/bin2c/bin2c.c; }) ];
    programName = "bin2c";
  };

  bsdiff = link {
    objects = [ (compileC { main = ./src/bsdiff-4.3/bsdiff.c; buildInputs = [ pkgs.bzip2 ]; }) ];
    programName = "bsdiff";
    buildInputs = [ pkgs.bzip2 ];
    flags = "-lbz2";
  };

  bspatch = link {
    objects = [ (compileC { main = ./src/bsdiff-4.3/bspatch.c; buildInputs = [ pkgs.bzip2 ]; }) ];
    programName = "bspatch";
    buildInputs = [ pkgs.bzip2 ];
    flags = "-lbz2";
  };

  libformat = makeLibrary {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src ];
      })
      [ ./src/boost/format/format_implementation.cc
        ./src/boost/format/free_funcs.cc
        ./src/boost/format/parsing.cc
      ];
    libraryName = "format";
  };

  libutil = makeLibrary {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/libutil ./src ./. ];
        buildInputs = [ pkgs.openssl ];
      })
      [ ./src/libutil/util.cc
        ./src/libutil/hash.cc
        ./src/libutil/serialise.cc
        ./src/libutil/archive.cc
        ./src/libutil/xml-writer.cc
        ./src/libutil/immutable.cc
      ];
    libraryName = "util";
  };

  libstore = makeLibrary {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/libstore ./src/libutil ./src ./. ];
        buildInputs = [ pkgs.sqlite ];
        cFlags = "-DNIX_STORE_DIR=\"/nix/store\" -DNIX_DATA_DIR=\"/home/eelco/Dev/nix/inst/share\" -DNIX_STATE_DIR=\"/nix/var/nix\" -DNIX_LOG_DIR=\"/foo\" -DNIX_CONF_DIR=\"/foo\" -DNIX_LIBEXEC_DIR=\"/foo\" -DNIX_BIN_DIR=\"/home/eelco/Dev/nix/inst/bin\"";
      })
      [ ./src/libstore/store-api.cc
        ./src/libstore/local-store.cc
        ./src/libstore/remote-store.cc
        ./src/libstore/derivations.cc
        ./src/libstore/build.cc
        ./src/libstore/misc.cc
        ./src/libstore/globals.cc
        ./src/libstore/references.cc
        ./src/libstore/pathlocks.cc
        ./src/libstore/gc.cc
        ./src/libstore/optimise-store.cc
      ];
    libraryName = "store";
  };

  libmain = makeLibrary {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/libmain ./src/libstore ./src/libutil ./src ./. ];
      })
      [ ./src/libmain/shared.cc ];
    libraryName = "main";
  };

  nix_hash = link {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/nix-hash ./src/libmain ./src/libstore ./src/libutil ./src ./. ];
      })
      [ ./src/nix-hash/nix-hash.cc
      ];
    libraries = [ libformat libutil libstore libmain ];
    buildInputs = [ pkgs.openssl pkgs.sqlite ];
    flags = "-lssl -lsqlite3 -lstdc++";
    programName = "nix-hash";
  };

  nix_store = link {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/nix-store ./src/libmain ./src/libstore ./src/libutil ./src ./. ];
      })
      [ ./src/nix-store/nix-store.cc
        ./src/nix-store/dotgraph.cc
        ./src/nix-store/xmlgraph.cc
      ];
    libraries = [ libformat libutil libstore libmain ];
    buildInputs = [ pkgs.openssl pkgs.sqlite ];
    flags = "-lssl -lsqlite3 -lstdc++";
    programName = "nix-store";
  };

  libexpr = makeLibrary {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/libexpr ./src/libstore ./src/libutil ./src ./. ];
      })
      [ ./src/libexpr/nixexpr.cc
        ./src/libexpr/eval.cc
        ./src/libexpr/primops.cc
        ./src/libexpr/lexer-tab.cc
        ./src/libexpr/parser-tab.cc
        ./src/libexpr/get-drvs.cc
        ./src/libexpr/attr-path.cc
        ./src/libexpr/value-to-xml.cc
        ./src/libexpr/common-opts.cc
        ./src/libexpr/names.cc
      ];
    libraryName = "expr";
  };

  nix_instantiate = link {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/nix-instantiate ./src/libexpr ./src/libmain ./src/libstore ./src/libutil ./src ./. ];
      })
      [ ./src/nix-instantiate/nix-instantiate.cc ];
    libraries = [ libformat libutil libstore libmain libexpr ];
    buildInputs = [ pkgs.openssl pkgs.sqlite ];
    flags = "-lssl -lsqlite3 -lstdc++";
    programName = "nix-instantiate";
  };

  nix_env = link {
    objects =
      map (fn: compileC {
        main = fn;
        localIncludePath = [ ./src/nix-env ./src/libexpr ./src/libmain ./src/libstore ./src/libutil ./src ./. ];
      })
      [ ./src/nix-env/nix-env.cc
        ./src/nix-env/profiles.cc
        ./src/nix-env/user-env.cc
      ];
    libraries = [ libformat libutil libstore libmain libexpr ];
    buildInputs = [ pkgs.openssl pkgs.sqlite ];
    flags = "-lssl -lsqlite3 -lstdc++";
    programName = "nix-env";
  };

  all = [ bsdiff bspatch nix_hash nix_store nix_instantiate nix_env ];

}
