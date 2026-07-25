# Cyclic attrset: the deep check must not stack overflow.
let
  a = {
    x = a;
  };
in
builtins.seq a.x (a == a)
