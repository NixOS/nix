let {

  inherit (import ../../lib) compileC link;

  hello = link {objects = compileC {
    main = ./foo/hello.c;
    localIncludes = [
      [./foo/fnord/indirect.h "fnord/indirect.h"]
      [./bar/hello.h "fnord/../../bar/hello.h"]
    ];
  };};

  body = [hello];
}
