with import ./config.nix;

mkDerivation {
  name = "store-kill-build";
  __contentAddressed = true;
  outputHashMode = "recursive";
  outputHashAlgo = "sha256";
  buildCommand = ''
    sleep 60
    mkdir "$out"
  '';
}
