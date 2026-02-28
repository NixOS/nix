let
  drv1 = derivation {
    name = "test1";
    builder = "/bin/false";
    system = "x86_64-linux";
  };
  drv2 = derivation {
    name = "test2";
    builder = "/bin/false";
    system = "x86_64-linux";
  };
  # Create a string with multiple context items by concatenating paths from different derivations
  multipleContexts = "${drv1.outPath}${drv2.outPath}";
in
builtins.derivationOf multipleContexts
