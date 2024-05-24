let
  package = {
    type = "derivation";
    name = "cachix-1.7.3";
    system = builtins.currentSystem;
    outputs = [ "out" ];
    # Illegal, because does not end in `.drv`
    drvPath = "${builtins.storeDir}/8qlfcic10lw5304gqm8q45nr7g7jl62b-cachix-1.7.3-bin";
    outputName = "out";
    outPath = "${builtins.storeDir}/8qlfcic10lw5304gqm8q45nr7g7jl62b-cachix-1.7.3-bin";
    out = package;
  };
in
package
