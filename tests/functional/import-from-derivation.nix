with import <config>;

rec {
  bar = mkDerivation {
    name = "bar";
    builder = builtins.toFile "builder.sh" ''
      echo 'builtins.add 123 456' > $out
    '';
  };

  value =
    # Test that pathExists can check the existence of /nix/store paths
    assert builtins.pathExists bar;
    import bar;

  result = mkDerivation {
    name = "foo";
    builder = builtins.toFile "builder.sh" ''
      echo -n FOO${toString value} > $out
    '';
  };

  addPath = mkDerivation {
    name = "add-path";
    src = builtins.filterSource (path: type: true) result;
    builder = builtins.toFile "builder.sh" ''
      echo -n BLA$(cat $src) > $out
    '';
  };

  step1 = mkDerivation {
    name = "step1";
    buildCommand = ''
      mkdir -p $out
      echo 'foo' > $out/bla
    '';
  };

  addPathExpr = mkDerivation {
    name = "add-path";
    inherit step1;
    buildCommand = ''
      mkdir -p $out
      echo "builtins.path { path = \"$step1\"; sha256 = \"7ptL+pnrZXnSa5hwwB+2SXTLkcSb5264WGGokN8OXto=\"; }" > $out/default.nix
    '';
  };

  importAddPathExpr = import addPathExpr;
}
