# Mutual cycle between attrset and list: the deep check must not stack overflow.
let
  a = {
    x = b;
  };
  b = [ a ];
in
builtins.seq a.x (builtins.seq (builtins.elemAt b 0) (a == a))
