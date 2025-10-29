with import ./config.nix;

rec {

  a = mkDerivation {
    name = "issue-13247-a";
    builder = builtins.toFile "builder.sh" ''
      mkdir $out
      test -z $all
      echo "output" > $out/file
    '';
  };

  # Same output, different drv
  a-prime = mkDerivation {
    name = "issue-13247-a";
    builder = builtins.toFile "builder.sh" ''
      echo 'will make the same stuff as `a`, but different drv hash'

      mkdir $out
      test -z $all
      echo "output" > $out/file
    '';
  };

  # Multiple outputs in a derivation that depends on other derivations
  f =
    dep:
    mkDerivation {
      name = "use-a-more-outputs";
      outputs = [
        "first"
        "second"
      ];
      inherit dep;
      builder = builtins.toFile "builder.sh" ''
        ln -s $dep/file $first
        ln -s $first $second
      '';
    };

  use-a-more-outputs = f a;

  use-a-prime-more-outputs = f a-prime;

}
