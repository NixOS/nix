# Value identity optimization masks function comparison: no warning expected.
# The pointer equality check short-circuits before we reach the function case.
let
  f = x: x;
in
{
  a = f;
} == {
  a = f;
}
