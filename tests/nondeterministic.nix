with import ./config.nix;

{

  nondeterministic = mkDerivation {
    name = "non-deterministic";
    buildCommand = "mkdir $out; dd if=/dev/urandom of=$out/file bs=3 count=1; echo building non-deterministic derivation";
  };

}
