# Pointer equality masks function comparison, but the lint looks one
# level into containers and detects functions there.
let
  f = x: x;
in
{
  a = f;
} == {
  a = f;
}
