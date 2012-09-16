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
        file = ./file;
      }).drvPath;
      coerceToString2 = (derivation {
        name = '''coerceToString''';
        builder = '''''${failBuilder}''';
        file = ${bazOutPath}/file;
      }).drvPath;

      isValidPath1 = builtins.storePath ./file;
      isValidPath2 = builtins.storePath ${bazOutPath}/file;

      pathExists1 = builtins.pathExists ./file;
      pathExists2 = builtins.pathExists ${bazOutPath}/file;

      readFile1 = builtins.readFile ./file;
      readFile2 = builtins.readFile ${bazOutPath}/file;

      filterSource1 = builtins.filterSource (p: t: baseNameOf p != '''a.nix''') ./.;
      filterSource2 = builtins.filterSource (p: t: baseNameOf p != '''a.nix''') ${bazOutPath};
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
