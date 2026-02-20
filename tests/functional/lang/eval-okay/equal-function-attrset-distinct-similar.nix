# Distinct but not identical functions in attribute set compare as unequal
# See https://nix.dev/manual/nix/latest/language/operators#equality
{ a = (x: x); } == { a = (x: x); }
