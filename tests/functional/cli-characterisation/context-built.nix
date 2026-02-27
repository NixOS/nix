# Built context: derivation output reference
with import ../config.nix;
let
  drv = mkDerivation {
    name = "helper-1.0";
    buildCommand = "mkdir -p $out";
  };
in
{
  type = "derivation";
  name = "${drv}";
  outPath = "/nix/store/fake";
}
