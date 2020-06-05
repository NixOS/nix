with import ./config.nix;

let

  input1 = mkDerivation {
    name = "build-hook-input-1";
    builder = ./dependencies.builder1.sh;
    requiredSystemFeatures = ["foo"];
  };

  input2 = mkDerivation {
    name = "build-hook-input-2";
    builder = ./dependencies.builder2.sh;
  };

in

  mkDerivation {
    name = "build-hook";
    builder = ./dependencies.builder0.sh;
    input1 = " " + input1 + "/.";
    input2 = " ${input2}/.";
  }
