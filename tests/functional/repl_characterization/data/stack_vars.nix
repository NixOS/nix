let
  a = builtins.trace "before inner break" (
    let meow' = 3; in builtins.break { msg = "hello"; }
  );
  b = builtins.trace "before outer break" (
    let meow = 2; in builtins.break a
  );
in
  b
