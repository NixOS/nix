with import ./lib.nix;

let
  n1 = builtins.floor 23.5;
  n2 = builtins.ceil 23.5;
  n3 = builtins.floor 23;
  n4 = builtins.ceil 23;
in
  builtins.concatStringsSep ";" (map toString [ n1 n2 n3 n4 ])
