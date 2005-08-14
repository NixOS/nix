with (import ../../../lib);

let {
  inherit (import ../aterm {}) libATerm;

  compileTest = main: link {
    objects = [(compileC {inherit main; cFlags = "-I../aterm";})];
    libraries = libATerm;
  };

  body = [
    (compileTest ./fib.c)
    (compileTest ./primes.c)
  ];
}
