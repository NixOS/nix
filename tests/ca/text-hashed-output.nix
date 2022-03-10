with import ./config.nix;

# A simple content-addressed derivation.
# The derivation can be arbitrarily modified by passing a different `seed`,
# but the output will always be the same
rec {
  root = mkDerivation {
    name = "text-hashed-root";
    buildCommand = ''
      set -x
      echo "Building a CA derivation"
      mkdir -p $out
      echo "Hello World" > $out/hello
    '';
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
  };
  dependent = mkDerivation {
    name = "text-hashed-root.drv";
    buildCommand = ''
      echo "Copying the derivation"
      cp ${root.drvPath} $out
    '';
    __contentAddressed = true;
    outputHashMode = "text";
    outputHashAlgo = "sha256";
  };
}
