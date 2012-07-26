attrs @ { drvPath, outputs, ... }:

let

  commonAttrs = (builtins.listToAttrs outputsList) //
    { all = map (x: x.value) outputsList;
      inherit drvPath;
      type = "derivation";
    };

  outputToAttrListElement = outputName:
    { name = outputName;
      value = commonAttrs // {
        outPath = builtins.getAttr outputName attrs;
        inherit outputName;
      };
    };
    
  outputsList = map outputToAttrListElement outputs;
    
in (builtins.head outputsList).value
