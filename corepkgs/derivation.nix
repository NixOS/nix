attrs:

let

  strict = derivationStrict attrs;
  
  attrValues = attrs:
    map (name: builtins.getAttr name attrs) (builtins.attrNames attrs);
    
  outputToAttrListElement = outputName:
    { name = outputName;
      value = attrs // {
        outPath = builtins.getAttr outputName strict;
        drvPath = strict.drvPath;
        type = "derivation";
        currentOutput = outputName;
      } // outputsAttrs // { all = allList; };
    };
    
  outputsList =
    if attrs ? outputs
    then map outputToAttrListElement attrs.outputs
    else [ (outputToAttrListElement "out") ];
    
  outputsAttrs = builtins.listToAttrs outputsList;
  
  allList = attrValues outputsAttrs;
  
  head = if attrs ? outputs then builtins.head attrs.outputs else "out";
  
in builtins.getAttr head outputsAttrs
