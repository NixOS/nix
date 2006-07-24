let {

  f = {x, y : ["baz" "bat"]}: x + y;

  body = f {x = "foo"; y = "bar";};

}
