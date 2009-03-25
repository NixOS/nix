with import ./config.nix;

rec {

  fail = mkDerivation {
    name = "fail";
    builder = builtins.toFile "builder.sh" "echo FAIL; exit 1";
  };

  succeed = mkDerivation {
    name = "succeed";
    builder = builtins.toFile "builder.sh" "echo SUCCEED; mkdir $out";
  };

  depOnFail = mkDerivation {
    name = "dep-on-fail";
    builder = builtins.toFile "builder.sh" "echo URGH; mkdir $out";
    inputs = [fail succeed];
  };

}
