with (import ../../../lib);

let {
  inherit (import ../aterm {}) libATerm;

  compileTest = main: link {
    objects = [(compileC {inherit main; localIncludePath = [ ../aterm ];})];
    libraries = libATerm;
  };

  body = [
    (compileTest ./fib.c)
    (compileTest ./primes.c)
  ];
}
