# Opaque context with a .drv store path (via unsafeDiscardOutputDependency roundtrip)
with import ../config.nix;
let
  drv = mkDerivation {
    name = "helper-1.0";
    buildCommand = "mkdir -p $out";
  };
in
{
  type = "derivation";
  name = builtins.unsafeDiscardOutputDependency (builtins.addDrvOutputDependencies drv.drvPath);
  outPath = "/nix/store/fake";
}
