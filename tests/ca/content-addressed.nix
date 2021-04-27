with import ../config.nix;

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
    name = "rootCA";
    outputs = [ "out" "dev" "foo"];
    buildCommand = ''
      echo "building a CA derivation"
      echo "The seed is ${toString seed}"
      mkdir -p $out
      echo ${rootLegacy}/hello > $out/dep
      ln -s $out $out/self
      # test symlinks at root
      ln -s $out $dev
      ln -s $out $foo
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
      cat ${rootCA}/self/dep
      echo ${rootCA}/self/dep > $out/dep
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
  dependentNonCA = mkDerivation {
    name = "dependent-non-ca";
    buildCommand = ''
      echo "Didn't cut-off"
      echo "building dependent-non-ca"
      mkdir -p $out
      echo ${rootCA}/non-ca-hello > $out/dep
    '';
  };
  dependentFixedOutput = mkDerivation {
    name = "dependent-fixed-output";
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
    outputHash = "sha256-QvtAMbUl/uvi+LCObmqOhvNOapHdA2raiI4xG5zI5pA=";
    buildCommand = ''
      cat ${dependentCA}/dep
      echo foo > $out
    '';

  };
}
