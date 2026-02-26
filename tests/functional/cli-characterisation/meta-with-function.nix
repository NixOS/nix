with import ../config.nix;
mkDerivation {
  name = "meta-with-function-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    bad = x: x;
  };
}
