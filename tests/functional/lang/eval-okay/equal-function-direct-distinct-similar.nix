# Direct comparison of distinct but not identical functions returns false
# See https://nix.dev/manual/nix/latest/language/operators#equality
(x: x) == (x: x)
