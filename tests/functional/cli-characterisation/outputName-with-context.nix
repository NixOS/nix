with import ../config.nix;
let
  normal = mkDerivation {
    name = "normal-1.0";
    buildCommand = "mkdir -p $out";
  };
in
{
  type = "derivation";
  name = "outputName-with-context-1.0";
  outPath = "/nix/store/fake";
  outputName = "${normal}";
}
