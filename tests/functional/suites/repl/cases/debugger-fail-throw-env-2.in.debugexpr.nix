let
  x.a = 1;
in
with x;
(_: builtins.seq x.a (throw "oh snap")) x.a
