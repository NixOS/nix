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
  wrapper = mkDerivation {
    name = "use-dynamic-drv-in-non-dynamic-drv";
    buildCommand = ''
      echo "Copying the output of the dynamic derivation"
      cp -r ${builtins.outputOf producingDrv.outPath "out"} $out
    '';
  };
}
