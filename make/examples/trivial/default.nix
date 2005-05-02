let {

  inherit (import ../../lib) compileC link;

  hello = link {objects = compileC {main = ./hello.c;};};

  body = [hello];
}
