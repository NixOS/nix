{sharedLib ? true}:

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
    forSharedLib = sharedLib;
  };

  libATerm = makeLibrary {
    libraryName = "ATerm";
    objects = map compile sources;
    inherit sharedLib;
  };

}
