with import ./lib.nix;

let xs = range 10 40; in

[ (builtins.elem 23 xs) (builtins.elem 42 xs) (builtins.elemAt xs 20) ]

