with import ../config.nix;
let
  normal = mkDerivation {
    name = "normal-1.0";
    buildCommand = "mkdir -p $out";
  };
in
mkDerivation {
  name = "outputsToInstall-with-context-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    outputsToInstall = [ "${normal}" ];
  };
}
