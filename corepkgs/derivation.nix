/* This is the implementation of the ‘derivation’ builtin function.
   It's actually a wrapper around the ‘derivationStrict’ primop. */

drvAttrs @ { outputs ? [ "out" ], ... }:

let

  strict = derivationStrict drvAttrs;
  
  commonAttrs = drvAttrs // (builtins.listToAttrs outputsList) // { all = map (x: x.value) outputsList; };

  outputToAttrListElement = outputName:
    { name = outputName;
      value = commonAttrs // {
        outPath = builtins.getAttr outputName strict;
        drvPath = strict.drvPath;
        type = "derivation";
        currentOutput = outputName;
      };
    };
    
  outputsList = map outputToAttrListElement outputs;
    
in (builtins.head outputsList).value
