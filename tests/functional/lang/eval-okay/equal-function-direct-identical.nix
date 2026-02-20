# Direct comparison of identical function returns false
# See https://nix.dev/manual/nix/latest/language/operators#equality
let
  f = x: x;
in
f == f
