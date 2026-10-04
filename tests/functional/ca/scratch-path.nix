with import ./config.nix;

# content depends on $out beyond rewritable self references
mkDerivation {
  name = "out-dependent";
  buildCommand = "mkdir $out; echo $out | cksum > $out/sum";
  __contentAddressed = true;
  outputHashMode = "recursive";
  outputHashAlgo = "sha256";
}
