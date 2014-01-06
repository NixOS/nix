let
  throw = abort "Error!";
in (builtins.tryEval <foobaz>).success
