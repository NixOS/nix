let
  package = {
    type = "derivation";
    name = "cachix-1.7.3";
    system = builtins.currentSystem;
    outputs = [ "out" ];
    # Illegal, because does not end in `.drv`
    drvPath = "${builtins.storeDir}/2chwzswhhmpxbgc981i2vcz7xj4d1in9-cachix-1.7.3-bin";
    outputName = "out";
    outPath = "${builtins.storeDir}/2chwzswhhmpxbgc981i2vcz7xj4d1in9-cachix-1.7.3-bin";
    out = package;
  };
in
package
