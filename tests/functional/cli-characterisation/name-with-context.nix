with import ../config.nix;
let
  normal = mkDerivation {
    name = "normal-1.0";
    buildCommand = "mkdir -p $out";
  };
in
{
  type = "derivation";
  name = "${normal}";
  outPath = "/nix/store/fake";
}
