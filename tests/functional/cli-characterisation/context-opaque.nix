# Opaque context: plain store path reference from file interpolation
{
  type = "derivation";
  name = "${builtins.toFile "x" ""}";
  outPath = "/nix/store/fake";
}
