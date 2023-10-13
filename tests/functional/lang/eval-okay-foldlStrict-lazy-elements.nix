# Tests that the rhs argument of op is not forced unconditionally
let
  lst = builtins.foldl'
    (acc: x: acc ++ [ x ])
    [ ]
    [ 42 (throw "this shouldn't be evaluated") ];
in

builtins.head lst
