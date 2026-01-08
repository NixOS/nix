let
  drv = derivation {
    name = "test";
    builder = "/bin/false";
    system = "x86_64-linux";
  };
in
builtins.derivationOf drv.outPath == drv.drvPath
