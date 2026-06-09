with import ../config.nix;
mkDerivation {
  name = "bad-outputs-to-install-type-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    outputsToInstall = "out"; # should be a list
  };
}
