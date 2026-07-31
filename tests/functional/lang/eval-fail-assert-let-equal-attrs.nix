assert
  let
    x = {
      b = 1;
    };
    y = {
      b = 2;
    };
  in
  x == y;
abort "unreachable"
