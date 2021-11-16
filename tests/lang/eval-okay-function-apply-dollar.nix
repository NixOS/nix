let
  a = 5;
  b = 1;
  c = 2;
  concat = a: b: "${a}${b}";
  nested.attr.ab = 10;

  add = a: b: a + b;
in add nested.attr.${concat "a" "b"} $ add b $ add a c
