with import ./config.nix;

{ seed }:
# A simple content-addressed derivation.
# The derivation can be arbitrarily modified by passing a different `seed`,
# but the output will always be the same
mkDerivation {
  name = "simple-content-addressed";
  builder = ./simple.builder.sh;
  PATH = "";
  goodPath = "${path}:${toString seed}";
  contentAddressed = true;
}
