with import ./config.nix;

rec {

  impure = mkDerivation {
    name = "impure";
    outputs = [ "out" "stuff" ];
    buildCommand =
      ''
        x=$(< $TEST_ROOT/counter)
        mkdir $out $stuff
        echo $x > $out/n
        ln -s $out/n $stuff/bla
        printf $((x + 1)) > $TEST_ROOT/counter
      '';
    __impure = true;
    outputHashAlgo = "sha256";
    outputHashMode = "recursive";
    impureEnvVars = [ "TEST_ROOT" ];
  };

  impureOnImpure = mkDerivation {
    name = "impure-on-impure";
    buildCommand =
      ''
        x=$(< ${impure}/n)
        mkdir $out
        printf X$x > $out/n
        ln -s ${impure.stuff} $out/symlink
        ln -s $out $out/self
      '';
    __impure = true;
    outputHashAlgo = "sha256";
    outputHashMode = "recursive";
  };

  # This is not allowed.
  inputAddressed = mkDerivation {
    name = "input-addressed";
    buildCommand =
      ''
        cat ${impure} > $out
      '';
  };

}
