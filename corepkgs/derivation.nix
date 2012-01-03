attrs:

let

  strict = derivationStrict attrs;
  
  attrValues = attrs:
    map (name: builtins.getAttr name attrs) (builtins.attrNames attrs);
    
  outputToAttrListElement = output:
    { name = output;
      value = attrs // {
        outPath = builtins.getAttr (output + "Path") strict;
        drvPath = strict.drvPath;
        type = "derivation";
        currentOutput = output;
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
