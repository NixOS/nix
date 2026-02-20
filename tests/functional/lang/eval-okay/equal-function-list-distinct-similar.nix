# Distinct but not identical functions in list compare as unequal
# See https://nix.dev/manual/nix/latest/language/operators#equality
[ (x: x) ] == [ (x: x) ]
