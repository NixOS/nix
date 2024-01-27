let
  inherit (builtins.trace "used" { a = 1; b = 2; }) a b;
  x.c = 3;
  y.d = 4;
in
  [ a b rec { x.c = []; inherit (x) c; inherit (y) d; __overrides.y.d = []; } ]
