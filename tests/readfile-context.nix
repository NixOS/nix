with import ./config.nix;

let

  input = mkDerivation {
    name = "dependencies-input-1";
    builder = ./dependencies.builder1.sh;
  };

  dependent = mkDerivation {
    name = "dependent";
    builder = ./readfile-context.builder.sh;
    input = "${input}/foo";
  };

  readDependent = mkDerivation {
    name = "read-dependent";
    builder = ./readfile-context.builder.sh;
    input = builtins.readFile dependent;
  };

in readDependent
