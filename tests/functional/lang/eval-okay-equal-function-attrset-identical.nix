# Function comparison in attribute set uses value identity optimization
# See https://nix.dev/manual/nix/latest/language/operators#value-identity-optimization
let
  f = x: x;
in
{
  a = f;
} == {
  a = f;
}
