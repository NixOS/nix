let
  aString = "a";

  bString = "b";
in {
  hasAttrs = { a.b = null; } ? ${aString}.b;

  selectAttrs = { a.b = true; }.a.${bString};

  selectOrAttrs = { }.${aString} or true;

  binds = { ${aString}.${bString} = true; }.a.b;

  "inherit" = let a = true; b = false; in { inherit ${aString} b; }.a;

  inheritFrom = let foo = { a = false; b = true; }; in { inherit (foo) a ${bString}; }.b;
}
