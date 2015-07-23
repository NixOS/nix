with builtins;

[ (any (x: x == 1) [])
  (any (x: x == 1) [2 3 4])
  (any (x: x == 1) [1 2 3 4])
  (any (x: x == 1) [4 3 2 1])
  (all (x: x == 1) [])
  (all (x: x == 1) [1])
  (all (x: x == 1) [1 2 3])
  (all (x: x == 1) [1 1 1])
]
