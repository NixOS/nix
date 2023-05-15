with import ./config.nix;

# A simple content-addressed derivation.
# The derivation can be arbitrarily modified by passing a different `seed`,
# but the output will always be the same
rec {
  hello = mkDerivation {
    name = "hello";
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
  producingDrv = mkDerivation {
    name = "hello.drv";
    buildCommand = ''
      echo "Copying the derivation"
      cp ${builtins.unsafeDiscardOutputDependency hello.drvPath} $out
    '';
    __contentAddressed = true;
    outputHashMode = "text";
    outputHashAlgo = "sha256";
  };
}
