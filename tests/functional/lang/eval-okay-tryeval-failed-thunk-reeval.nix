# Since Nix 2.34, errors are memoized
let
  # This attribute value will only be evaluated once.
  foo = builtins.trace "throwing" throw "nope";
in
# Trigger and catch the error twice.
builtins.seq (builtins.tryEval foo).success builtins.seq (builtins.tryEval foo).success "done"
