with import ./config.nix;

let mkCADerivation = args: mkDerivation ({
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
} // args);
in

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
  rootCA = mkCADerivation {
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
  };
  dependentCA = mkCADerivation {
    name = "dependent";
    buildCommand = ''
      echo "building a dependent derivation"
      mkdir -p $out
      cat ${rootCA}/self/dep
      echo ${rootCA}/self/dep > $out/dep
    '';
  };
  transitivelyDependentCA = mkCADerivation {
    name = "transitively-dependent";
    buildCommand = ''
      echo "building transitively-dependent"
      cat ${dependentCA}/dep
      echo ${dependentCA} > $out
    '';
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
    outputHash = "sha512-7aJcmSuEuYP5tGKcmGY8bRr/lrCjJlOxP2mIUjO/vMQeg6gx/65IbzRWES8EKiPDOs9z+wF30lEfcwxM/cT4pw==";
    buildCommand = ''
      cat ${dependentCA}/dep
      echo foo > $out
    '';
  };
  runnable = mkCADerivation rec {
    name = "runnable-thing";
    buildCommand = ''
      mkdir -p $out/bin
      echo ${rootCA} # Just to make it depend on it
      echo "" > $out/bin/${name}
      chmod +x $out/bin/${name}
    '';
  };
}
