rec {

  inherit (import ../../../lib) compileC makeLibrary;

  sources = [
    ./afun.c
    ./aterm.c
    ./bafio.c
    ./byteio.c
    ./gc.c
    ./hash.c
    ./list.c
    ./make.c
    ./md5c.c
    ./memory.c
    ./tafio.c
    ./version.c
  ];

  compile = fn: compileC {
    main = fn;
    localIncludes = "auto";
  };

  libATerm = makeLibrary {
    libraryName = "ATerm";
    objects = map {function = compile; list = sources;};
  };

}
