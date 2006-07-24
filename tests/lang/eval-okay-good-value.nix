let {

  f = {x, y : ["baz" "bar" "bat"]}: x + y;

  body = f {x = "foo"; y = "bar";};

}
