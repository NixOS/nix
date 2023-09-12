with import ./config.nix;

rec {

  impure = mkDerivation {
    name = "impure";
    outputs = [ "out" "stuff" ];
    buildCommand =
      ''
        echo impure
        x=$(< $TEST_ROOT/counter)
        mkdir $out $stuff
        echo $x > $out/n
        ln -s $out/n $stuff/bla
        printf $((x + 1)) > $TEST_ROOT/counter
      '';
    __impure = true;
    impureEnvVars = [ "TEST_ROOT" ];
  };

  impureOnImpure = mkDerivation {
    name = "impure-on-impure";
    buildCommand =
      ''
        echo impure-on-impure
        x=$(< ${impure}/n)
        mkdir $out
        printf X$x > $out/n
        ln -s ${impure.stuff} $out/symlink
        ln -s $out $out/self
      '';
    __impure = true;
  };

  # This is not allowed.
  inputAddressed = mkDerivation {
    name = "input-addressed";
    buildCommand =
      ''
        cat ${impure} > $out
      '';
  };

  contentAddressed = mkDerivation {
    name = "content-addressed";
    buildCommand =
      ''
        echo content-addressed
        x=$(< ${impureOnImpure}/n)
        printf ''${x:0:1} > $out
      '';
    outputHashMode = "recursive";
    outputHash = "sha256-eBYxcgkuWuiqs4cKNgKwkb3vY/HR0vVsJnqe8itJGcQ=";
  };

  inputAddressedAfterCA = mkDerivation {
    name = "input-addressed-after-ca";
    buildCommand =
      ''
        cat ${contentAddressed} > $out
      '';
  };
}
