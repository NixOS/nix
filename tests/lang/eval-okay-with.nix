let {

  a = "xyzzy";

  as = {
    a = "foo";
    b = "bar";
  };

  bs = {
    a = "bar";
  };

  x = with as; a + b;

  y = with as; with bs; a + b;

  body = x + y;
}
