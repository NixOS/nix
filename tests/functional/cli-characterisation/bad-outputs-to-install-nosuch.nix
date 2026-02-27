with import ../config.nix;
mkDerivation {
  name = "bad-outputs-to-install-nosuch-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    outputsToInstall = [ "nonexistent" ];
  };
}
