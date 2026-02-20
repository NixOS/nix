# This reproduces https://github.com/NixOS/nix/issues/10993, for lists
# $ nix run nix/2.23.1 -- eval --expr '"" + (let v = [ [ 1 2 3 4 5 6 7 8 ] [1 2 3 4]]; in builtins.deepSeq v v)'
# error:
#        … while evaluating a path segment
#          at «string»:1:6:
#             1| "" + (let v = [ [ 1 2 3 4 5 6 7 8 ] [1 2 3 4]]; in builtins.deepSeq v v)
#              |      ^
#
#        error: cannot coerce a list to a string: [ [ 1 2 3 4 5 6 7 8 ] [ 1 «4294967290 items elided» ] ]

""
+ (
  let
    v = [
      [
        1
        2
        3
        4
        5
        6
        7
        8
      ]
      [
        1
        2
        3
        4
      ]
    ];
  in
  builtins.deepSeq v v
)
