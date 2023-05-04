with builtins;

let
  l = [ 1 2 3 4 5 6 7 8 9 10 ];
in

sublist 0 0 l
++ sublist 0 10 l
++ sublist 0 1 l
++ sublist 11 10 l
