((x: as: {x}:
  rec {
    inherit (as) x;
    y = x;
  }
) 2 {x = 4;} {x = 3;}).y
