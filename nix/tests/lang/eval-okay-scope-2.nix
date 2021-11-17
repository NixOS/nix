((x: {x}:
  rec {
    x = 1;
    y = x;
  }
) 2 {x = 3;}).y
