let
  pkg = derivation {
    name = "test";
    builder = "/bin/false";
    system = "x86_64-linux";
  };
in
builtins.derivationOf pkg == pkg.drvPath
