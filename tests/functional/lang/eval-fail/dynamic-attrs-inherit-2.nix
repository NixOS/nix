let
  a.b = 1;
in
{
  inherit (a) ${"b" + ""};
}
