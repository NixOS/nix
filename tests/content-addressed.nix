with import ./config.nix;

{ seed ? 0 }:
# A simple content-addressed derivation.
# The derivation can be arbitrarily modified by passing a different `seed`,
# but the output will always be the same
rec {
  contentAddressed = mkDerivation {
    name = "simple-content-addressed";
    buildCommand = ''
      set -x
      echo "Building a CA derivation"
      echo "The seed is ${toString seed}"
      mkdir -p $out
      echo "Hello World" > $out/hello
    '';
    __contentAddressed = true;
  };
  dependent = mkDerivation {
    name = "dependent";
    buildCommand = ''
      echo "building a dependent derivation"
      mkdir -p $out
      echo ${contentAddressed}/hello > $out/dep
    '';
  };
  transitivelyDependent = mkDerivation {
    name = "transitively-dependent";
    buildCommand = ''
      echo "building transitively-dependent"
      cat ${dependent}/dep
      echo ${dependent} > $out
    '';
  };
}
