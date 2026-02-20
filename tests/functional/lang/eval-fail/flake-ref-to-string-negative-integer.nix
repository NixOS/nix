let
  n = -1;
in
builtins.seq n (
  builtins.flakeRefToString {
    type = "github";
    owner = "NixOS";
    repo = n;
    ref = "23.05";
    dir = "lib";
  }
)
