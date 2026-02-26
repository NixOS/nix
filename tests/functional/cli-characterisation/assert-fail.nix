with import ../config.nix;
mkDerivation {
  name =
    assert false;
    "assert-fail-1.0";
  buildCommand = "mkdir -p $out";
}
