with import ./lib.nix;

builtins.foldl' (x: y: x + y) 0 (range 1 1000)
