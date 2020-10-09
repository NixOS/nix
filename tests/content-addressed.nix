with import ./config.nix;

{ seed ? 0 }:
# A simple content-addressed derivation.
# The derivation can be arbitrarily modified by passing a different `seed`,
# but the output will always be the same
rec {
  rootLegacy = mkDerivation {
    name = "simple-input-addressed";
    buildCommand = ''
      set -x
      echo "Building a legacy derivation"
      mkdir -p $out
      echo "Hello World" > $out/hello
    '';
  };
  rootCA = mkDerivation {
    name = "dependent";
    outputs = [ "out" "dev" ];
    buildCommand = ''
      echo "building a CA derivation"
      echo "The seed is ${toString seed}"
      mkdir -p $out
      echo ${rootLegacy}/hello > $out/dep
      # test symlink at root
      ln -s $out $dev
    '';
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
  };
  dependentCA = mkDerivation {
    name = "dependent";
    buildCommand = ''
      echo "building a dependent derivation"
      mkdir -p $out
      echo ${rootCA}/hello > $out/dep
    '';
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
  };
  transitivelyDependentCA = mkDerivation {
    name = "transitively-dependent";
    buildCommand = ''
      echo "building transitively-dependent"
      cat ${dependentCA}/dep
      echo ${dependentCA} > $out
    '';
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
  };
}
