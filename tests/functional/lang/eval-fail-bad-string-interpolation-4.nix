let
  # Basically a "billion laughs" attack, but toned down to simulated `pkgs`.
  ha = x: y: { a = x y; b = x y; c = x y; d = x y; e = x y; f = x y; g = x y; h = x y; j = x y; };
  has = ha (ha (ha (ha (x: x)))) "ha";
  # A large structure that has already been evaluated.
  pkgs = builtins.deepSeq has has;
in
# The error message should not be too long.
''${pkgs}''
