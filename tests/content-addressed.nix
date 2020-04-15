with import ./config.nix;

{ seed ? 0 }:
rec {
  # A simple content-addressed derivation.
  # The derivation can be arbitrarily modified by passing a different `seed`,
  # but the output will always be the same
  contentAddressed = mkDerivation {
    name = "simple-content-addressed";
    builder = ./simple.builder.sh;
    PATH = "";
    goodPath = "${path}:${toString seed}";
    __contentAddressed = true;
    MSG = "Hello from ${placeholder "out"}";
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
