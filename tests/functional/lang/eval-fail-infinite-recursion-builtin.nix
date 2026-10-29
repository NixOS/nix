# An infinite recursion that occurs in the context of another call that isn't part of the cycle
let
  f = builtins.add f 1;
in
toString f
