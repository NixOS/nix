{
  hashInvalidator ? "",
}:
with import ./config.nix;

let

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
      echo -n fod-input-${testSalt} > $out
    '';
    outputHashMode = "flat";
    outputHashAlgo = "sha256";
    outputHash = "${builtins.hashString "sha256" "fod-input-${testSalt}"}";
  };

in
mkDerivation {
  name = "dependencies-top";
  builder = ./dependencies.builder0.sh + "/FOOBAR/../.";
  input1 = input1 + "/.";
  input2 = "${input2}/.";
  input1_drv = input1;
  input2_drv = input2;
  input0_drv = input0;
  fod_input_drv = fod_input;
  meta.description = "Random test package";
}
