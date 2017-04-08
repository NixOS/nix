/* This is the implementation of the ‘derivation’ builtin function.
   It's actually a wrapper around the ‘derivationStrict’ primop. */

drvAttrs @ { outputs ? [ "out" ], __ignoreNulls ? false, ... }:

let

  strict = derivationStrict drvAttrs;

  commonAttrs = drvAttrs // (builtins.listToAttrs outputsList) //
    { all = map (x: x.value) outputsList;
      inherit drvAttrs;
    };

  outputToAttrListElement = outputName:
    { name = outputName;
      value = commonAttrs // {
        outPath = builtins.getAttr outputName strict;
        drvPath = strict.drvPath;
        type = "derivation";
        inherit outputName;
      };
    };

  outputsList = map outputToAttrListElement (
    if __ignoreNulls && (outputs == null) then [ "out" ] else outputs
  );

in (builtins.head outputsList).value
