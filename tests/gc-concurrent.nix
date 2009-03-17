with import ./config.nix;

rec {

  input1 = mkDerivation {
    name = "dependencies-input-1";
    builder = ./dependencies.builder1.sh;
  };

  input2 = mkDerivation {
    name = "dependencies-input-2";
    builder = ./dependencies.builder2.sh;
  };

  test1 = mkDerivation {
    name = "gc-concurrent";
    builder = ./gc-concurrent.builder.sh;
    inherit input1 input2;
  };

  test2 = mkDerivation {
    name = "gc-concurrent2";
    builder = ./gc-concurrent2.builder.sh;
    inherit input1 input2;
  };
  
}
