let {

  inherit (import ../../lib) compileC link;

  hello = link {programName = "hello"; objects = compileC {
    main = ./foo/hello.c;
    localIncludes = [
      [./foo/fnord/indirect.h "fnord/indirect.h"]
      [./bar/hello.h "fnord/../../bar/hello.h"]
    ];
  };};

  body = [hello];
}
