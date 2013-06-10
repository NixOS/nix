let
  aString = "a";

  bString = "b";
in {
  hasAttrs = { a.b = null; } ? ${aString}.b;

  selectAttrs = { a.b = true; }.a.${bString};

  selectOrAttrs = { }.${aString} or true;

  binds = { ${aString}.${bString} = true; }.a.b;

  recBinds = rec { ${aString} = true; }.a;

  multiAttrs = { ${aString} = true; ${bString} = false; }.a;
}
