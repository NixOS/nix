let
  d = 0;
  x = 1;
  y = { inherit d x; };
  z = { inherit (y) d x; };
in
[
  (builtins.unsafeGetAttrPos "d" y)
  (builtins.unsafeGetAttrPos "x" y)
  (builtins.unsafeGetAttrPos "d" z)
  (builtins.unsafeGetAttrPos "x" z)
]
