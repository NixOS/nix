assert include ./include-1.nix == 1;
assert let x = 100; in include ./include-1.nix == 1;
assert with { x = 123; }; include ./include-1.nix == 1;

assert let x = 2; in include ./include-2.nix == 3;
assert let x = 3; in include ./include-2.nix == 4;
assert let x = 3; in let x = 4; in include ./include-2.nix == 5;
assert let x = 3; in let x = 4; in include ./include-2.nix == 5;
assert let x = 6; in with { x = 7; }; include ./include-2.nix == 7;
assert with { x = 0; }; let x = 6; in with { x = 7; }; include ./include-2.nix == 7;
assert with { x = 0; }; let x = 6; in with { x = 7; }; let x = 7; in include ./include-2.nix == 8;
assert with { x = 8; }; include ./include-2.nix == 9;

assert (let x = 10; in include ./include-3.nix) == 3628800;

true
