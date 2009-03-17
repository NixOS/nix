with import ./config.nix;

let

  input1 = mkDerivation {
    name = "build-hook-input-1";
    builder = ./dependencies.builder1.sh;
  };

  input2 = mkDerivation {
    name = "build-hook-input-2";
    builder = ./dependencies.builder2.sh;
  };

in

  mkDerivation {
    name = "build-hook";
    builder = ./dependencies.builder0.sh;
    inherit input1 input2;
  }
