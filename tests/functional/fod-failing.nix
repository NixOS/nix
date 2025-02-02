with import ./config.nix;
rec {
  x1 = mkDerivation {
    name = "x1";
    builder = builtins.toFile "builder.sh" ''
      echo $name > $out
    '';
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x2 = mkDerivation {
    name = "x2";
    builder = builtins.toFile "builder.sh" ''
      echo $name > $out
    '';
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x3 = mkDerivation {
    name = "x3";
    builder = builtins.toFile "builder.sh" ''
      echo $name > $out
    '';
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x4 = mkDerivation {
    name = "x4";
    inherit x2 x3;
    builder = builtins.toFile "builder.sh" ''
      echo $x2 $x3
      exit 1
    '';
  };
}
