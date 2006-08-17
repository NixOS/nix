rec {

  x = 123;

  a = "foo";

  b = "bar";

  c = "foo" + "bar";

  f = {z, x : ["a" "b" ("c" + "d")], y : [true false]}: if y then x else z;

}