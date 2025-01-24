let
  drv0 = derivation {
    name = "fail";
    builder = "/bin/false";
    system = "x86_64-linux";
    outputs = [
      "out"
      "foo"
    ];
  };

  drv1 = derivation {
    name = "fail-2";
    builder = "/bin/false";
    system = "x86_64-linux";
    outputs = [
      "out"
      "foo"
    ];
  };

  combo-path = "${drv0.drvPath}${drv1.drvPath}";

in
builtins.addDrvOutputDependencies combo-path
