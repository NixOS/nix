with import ./config.nix;

rec {
  bar = mkDerivation {
    name = "bar";
    builder = builtins.toFile "builder.sh"
      ''
        echo 'builtins.add 123 456' > $out
      '';
  };

  value =
    # Test that pathExists can check the existence of /nix/store paths
    assert builtins.pathExists bar;
    import bar;

  result = mkDerivation {
    name = "foo";
    builder = builtins.toFile "builder.sh"
      ''
        echo -n FOO${toString value} > $out
      '';
  };

  addPath = mkDerivation {
    name = "add-path";
    src = builtins.filterSource (path: type: true) result;
    builder = builtins.toFile "builder.sh"
      ''
        echo -n BLA$(cat $src) > $out
      '';
  };
}
