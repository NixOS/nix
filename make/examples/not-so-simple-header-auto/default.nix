with import ../../lib;

let {

  hello = link {programName = "hello"; objects = compileC {
    main = ./foo/hello.c;
    localIncludes = "auto";
  };};

#  body = findIncludes {main = ./foo/hello.c;};

  body = [hello];
}
