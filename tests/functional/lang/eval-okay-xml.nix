rec {

  x = 123;

  y = 567.890;

  a = "foo";

  b = "bar";

  c = "foo" + "bar";

  f = {z, x, y}: if y then x else z;

  id = x: x;

  at = args@{x, y, z}: x;

  ellipsis = {x, y, z, ...}: x;

}
