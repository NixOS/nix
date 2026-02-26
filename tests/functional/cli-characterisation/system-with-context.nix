with import ../config.nix;
let
  normal = mkDerivation {
    name = "normal-1.0";
    buildCommand = "mkdir -p $out";
  };
in
{
  type = "derivation";
  name = "system-with-context-1.0";
  system = "${normal}";
  outPath = "/nix/store/fake";
}
