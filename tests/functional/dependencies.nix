{ hashInvalidator ? "" }:
with import ./config.nix;

let {

  input0 = mkDerivation {
    name = "dependencies-input-0";
    buildCommand = "mkdir $out; echo foo > $out/bar";
  };

  input1 = mkDerivation {
    name = "dependencies-input-1";
    buildCommand = "mkdir $out; echo FOO > $out/foo";
  };

  input2 = mkDerivation {
    name = "dependencies-input-2";
    buildCommand = ''
      mkdir $out
      echo BAR > $out/bar
      echo ${input0} > $out/input0
    '';
  };

  fod_input = mkDerivation {
    name = "fod-input";
    buildCommand = ''
      echo ${hashInvalidator}
      echo FOD > $out
    '';
    outputHashMode = "flat";
    outputHashAlgo = "sha256";
    outputHash = "1dq9p0hnm1y75q2x40fws5887bq1r840hzdxak0a9djbwvx0b16d";
  };

  body = mkDerivation {
    name = "dependencies-top";
    builder = ./dependencies.builder0.sh + "/FOOBAR/../.";
    input1 = input1 + "/.";
    input2 = "${input2}/.";
    input1_drv = input1;
    input2_drv = input2;
    input0_drv = input0;
    fod_input_drv = fod_input;
    meta.description = "Random test package";
  };

}
