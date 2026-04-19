let
  flakeRef1 = builtins.flakeRefToString {
    type = "github";
    owner = "NixOS";
    repo = "nixpkgs";
    ref = "23.05";
    dir = "lib";
  };

  flakeRef2 = builtins.flakeRefToString {
    type = "file";
    url = "file://${builtins.toFile "hello" "hello"}";
  };
in

assert !builtins.hasContext flakeRef1;
assert builtins.hasContext flakeRef2;

[
  flakeRef1
  flakeRef2
]
