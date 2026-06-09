with import ../config.nix;
mkDerivation {
  name = "bad-outputs-to-install-elem-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    outputsToInstall = [ 42 ]; # elements should be strings
  };
}
