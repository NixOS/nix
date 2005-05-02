let {

  inherit (import ../../lib) compileC link;

  hello = link {objects = compileC {
    main = ./hello.c;
    localIncludes = [ [./hello.h "hello.h"] ];
  };};

  body = [hello];
}
