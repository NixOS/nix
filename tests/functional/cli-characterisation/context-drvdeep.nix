# DrvDeep context: derivation path with full closure dependency
with import ../config.nix;
let
  drv = mkDerivation {
    name = "helper-1.0";
    buildCommand = "mkdir -p $out";
  };
in
{
  type = "derivation";
  name = builtins.addDrvOutputDependencies drv.drvPath;
  outPath = "/nix/store/fake";
}
