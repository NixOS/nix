with import ./config.nix;

let

  input = import ./simple.nix;

  dependent = mkDerivation {
    name = "dependent";
    builder = ./readfile-context.builder.sh;
    input = "${input}/hello";
  };

  readDependent = mkDerivation {
    name = "read-dependent";
    builder = ./readfile-context.builder.sh;
    input = builtins.readFile dependent;
  };

in readDependent
