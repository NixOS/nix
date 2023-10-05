with import ./lib.nix;

let

  attrs = {y = "y"; x = "x"; foo = "foo";} // rec {x = "newx"; bar = x;};

  names = builtins.attrNames attrs;

  values = map (name: builtins.getAttr name attrs) names;

in assert values == builtins.attrValues attrs; concat values
