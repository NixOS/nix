let
  c = {};
  b = 2;
in {
  a = 1;
  inherit b;
  inherit (c) d e;
  f = 3;
}
