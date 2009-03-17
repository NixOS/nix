with import ./config.nix;

let {

  input1 = mkDerivation {
    name = "dependencies-input-1";
    builder = ./dependencies.builder1.sh;
  };

  input2 = mkDerivation {
    name = "dependencies-input-2";
    builder = ./. ~ "dependencies.builder2.sh";
  };

  body = mkDerivation {
    name = "dependencies";
    builder = ./dependencies.builder0.sh  + "/FOOBAR/../.";
    input1 = input1 + "/.";
    inherit input2;
  };

}
