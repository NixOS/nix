with import ./config.nix;

let

  input1 = mkDerivation {
    name = "build-hook-input-1";
    buildCommand = "mkdir $out; echo FOO > $out/foo";
    requiredSystemFeatures = ["foo"];
  };

  input2 = mkDerivation {
    name = "build-hook-input-2";
    buildCommand = "mkdir $out; echo BAR > $out/bar";
  };

in

  mkDerivation {
    name = "build-hook";
    builder = ./dependencies.builder0.sh;
    input1 = " " + input1 + "/.";
    input2 = " ${input2}/.";
  }
