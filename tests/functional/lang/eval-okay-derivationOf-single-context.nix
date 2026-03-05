let
  drv = derivation {
    name = "single-test";
    builder = "/bin/false";
    system = "x86_64-linux";
  };
  # Explicitly test with a single context item
  singleContext = drv.outPath;
in
builtins.derivationOf singleContext == drv.drvPath
