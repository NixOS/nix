with import ./config.nix;

# A simple content-addressed derivation.
# The derivation can be arbitrarily modified by passing a different `seed`,
# but the output will always be the same
rec {
  stub = mkDerivation {
    name = "stub";
    buildCommand = ''
      echo stub > $out
    '';
  };
  wrapper = mkDerivation {
    name = "has-dynamic-drv-dep";
    buildCommand = ''
      exit 1 # we're not building this derivation
      ${builtins.outputOf stub.outPath "out"}
    '';
  };
}
