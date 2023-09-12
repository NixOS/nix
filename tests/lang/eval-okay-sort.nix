with builtins;

[ (sort lessThan [ 483 249 526 147 42 77 ])
  (sort (x: y: y < x) [ 483 249 526 147 42 77 ])
  (sort lessThan [ "foo" "bar" "xyzzy" "fnord" ])
  (sort (x: y: x.key < y.key)
    [ { key = 1; value = "foo"; } { key = 2; value = "bar"; } { key = 1; value = "fnord"; } ])
  (sort lessThan [
    [ 1 6 ]
    [ ]
    [ 2 3 ]
    [ 3 ]
    [ 1 5 ]
    [ 2 ]
    [ 1 ]
    [ ]
    [ 1 4 ]
    [ 3 ]
  ])
]
