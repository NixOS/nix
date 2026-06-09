with import ../config.nix;
let
  normal = mkDerivation {
    name = "normal-1.0";
    buildCommand = "mkdir -p $out";
  };
in
mkDerivation {
  name = "meta-with-drv-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    someDrv = normal;
  };
}
