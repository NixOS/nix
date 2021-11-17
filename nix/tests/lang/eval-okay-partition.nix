with import ./lib.nix;

builtins.partition
  (x: x / 2 * 2 == x)
  (builtins.concatLists [ (range 0 10) (range 100 110) ])
