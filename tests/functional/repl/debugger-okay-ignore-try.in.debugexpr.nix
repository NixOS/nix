let
  someFailingExpr = throw "fail";
  tried = (builtins.tryEval someFailingExpr);
in
let
  bricked = break tried;
in
bricked
