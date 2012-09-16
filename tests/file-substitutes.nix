with import ./config.nix;

let
  failBuilder = builtins.toFile "builder.sh" ''
    exit 1
  '';

  bar = mkDerivation {
    name = "bar";
    builder = failBuilder;
    baz = mkDerivation {
      name = "baz";
      builder = failBuilder;
    };
  };

  bazOutPath = builtins.unsafeDiscardStringContext bar.baz.outPath;

  substitutedExprString = ''
    let
      failBuilder = builtins.toFile '''builder.sh''' '''
        exit 1
      ''';
    in

    {
      evalFile1 = import ./a.nix;
      evalFile2 = import ${bazOutPath}/a.nix;

      coerceToString1 = (derivation {
        name = '''coerceToString''';
        builder = '''''${failBuilder}''';
        system = '''nix''';
        file = ./file;
      }).drvPath;
      coerceToString2 = (derivation {
        name = '''coerceToString''';
        builder = '''''${failBuilder}''';
        system = '''nix''';
        file = ${bazOutPath}/file;
      }).drvPath;
    }
  '';

  fileString = "A file";

  aDotNixString = "true";
in

{
  substitutedExpr = builtins.toFile "default.nix" substitutedExprString;

  file = builtins.toFile "file" fileString;

  aDotNix = builtins.toFile "a.nix" aDotNixString;

  imported = import bar.outPath;

  inherit bar substitutedExprString fileString aDotNixString;
}
