let {

  inherit (import ../../lib) compileC findIncludes link;

  hello = link {programName = "hello"; objects = compileC {
    main = ./foo/hello.c;
    localIncludes = import (findIncludes {main = toString ./foo/hello.c;});
  };};

  body = [hello];
}
