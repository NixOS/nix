with import ../config.nix;
let
  x = { inherit x; };
in
mkDerivation {
  name = "infinite-meta-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    description = "Has infinite recursion in meta";
    bad = x;
  };
}
