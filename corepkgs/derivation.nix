/* This is the implementation of the ‘derivation’ builtin function.
   It's actually a wrapper around the ‘derivationStrict’ primop. */

drvAttrs @ { outputs ? [ "out" ], ... }:

let
  strict = derivationStrict2 drvAttrs outputsMap;

  commonAttrs = drvAttrs // outputsMap //
    { all = builtins.attrValues outputsMap;
      inherit drvAttrs;
    };

  outputsMap = builtins.genAttrs outputs (outputName:
    commonAttrs // {
      outPath = strict.${outputName};
      drvPath = strict.drvPath;
      inputSrcs = strict.inputSrcs;
      inputDrvs = strict.inputDrvs;
      type = "derivation";
      inherit outputName;
    }
  );

in
  outputsMap.${builtins.head outputs}

