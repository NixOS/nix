let
  a = builtins.trace "before inner break" (
    builtins.break { msg = "hello"; }
  );
  b = builtins.trace "before outer break" (
    builtins.break a
  );
in
  b
