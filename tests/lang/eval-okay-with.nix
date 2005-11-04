let {

  a = "xyzzy";

  as = {
    a = "foo";
    b = "bar";
  };

  x = with as; a + b;

  body = x;
}
