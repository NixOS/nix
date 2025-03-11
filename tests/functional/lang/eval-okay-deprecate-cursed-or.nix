let
  # These are cursed and should warn
  cursed0 = builtins.length (let or = 1; in [ (x: x) or ]);
  cursed1 = let or = 1; in (x: x * 2) (x: x + 1) or;
  cursed2 = let or = 1; in { a = 2; }.a or (x: x) or;

  # These are uses of `or` as an identifier that are not cursed
  allowed0 = let or = (x: x); in map or [];
  allowed1 = let f = (x: x); or = f; in f (f or);
in
0
