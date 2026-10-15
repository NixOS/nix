# `tryEval` does not catch infinite recursion. An error from the `context`
# argument of `addErrorContext` must not turn it into an error that `tryEval`
# catches.
let
  n = {
    x = if (builtins.tryEval (builtins.addErrorContext "${throw "x"}" n.x)).success then 1 else 2;
  };
in
n.x
