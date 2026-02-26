with import ../config.nix;
mkDerivation {
  name = "simple-1.0";
  buildCommand = "mkdir -p $out";
}
