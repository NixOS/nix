let {

  f = {x, y : ["baz" "bar" z "bat"]}: x + y;

  body = f {x = "foo"; y = "bar";};

}
