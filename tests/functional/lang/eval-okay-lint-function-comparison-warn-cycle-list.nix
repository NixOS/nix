# Cyclic list: the deep check must not stack overflow.
let
  a = [ a ];
in
builtins.seq (builtins.elemAt a 0) (a == a)
