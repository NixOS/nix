with import ./config.nix;

{

  good = mkDerivation {
    name = "good";
    builder = builtins.toFile "builder"
      ''
        mkdir $out
        touch $out/good
      '';
  };

  bad = mkDerivation {
    name = "good";
    builder = builtins.toFile "builder"
      ''
        mkdir $out
        touch $out/bad
      '';
  };

}
