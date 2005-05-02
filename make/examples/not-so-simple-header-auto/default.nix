let {

  inherit (import ../../lib) compileC findIncludes link;

  hello = link {programName = "hello"; objects = compileC {
    main = ./foo/hello.c;
    localIncludes = "auto";
  };};

  body = [hello];
}
