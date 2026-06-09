let
  a = throw "nope";
  b = builtins.addErrorContext "forcing b" a;
  c = builtins.addErrorContext "forcing c" a;
  d = builtins.addErrorContext "forcing d" a;
in
# Since nix 2.34 errors are memoised. Trying to eval a failed thunk includes
# the trace from when it was first forced. When forcing a failed value it gets
# a fresh instance of the exceptions to avoid trace mutation.
builtins.seq (builtins.tryEval b) (builtins.seq (builtins.tryEval c) d)
