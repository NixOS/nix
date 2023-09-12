with import ./config.nix;

{ lockFifo ? null }:

rec {

  input1 = mkDerivation {
    name = "dependencies-input-1";
    buildCommand = "mkdir $out; echo FOO > $out/foo";
  };

  input2 = mkDerivation {
    name = "dependencies-input-2";
    buildCommand = "mkdir $out; echo BAR > $out/bar";
  };

  test1 = mkDerivation {
    name = "gc-concurrent";
    builder = ./gc-concurrent.builder.sh;
    inherit input1 input2;
    inherit lockFifo;
  };

  test2 = mkDerivation {
    name = "gc-concurrent2";
    builder = ./gc-concurrent2.builder.sh;
    inherit input1 input2;
  };

}
