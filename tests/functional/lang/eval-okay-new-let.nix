let

  f = z: 

    let
      x = "foo";
      y = "bar";
      body = 1; # compat test
    in
      z + x + y;

  arg = "xyzzy";

in f arg
