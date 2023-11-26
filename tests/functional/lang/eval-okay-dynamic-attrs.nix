let
  aString = "a";

  bString = "b";
in {
  hasAttrs = { a.b = null; } ? "${aString}".b;

  selectAttrs = { a.b = true; }.a."${bString}";

  selectOrAttrs = { }."${aString}" or true;

  binds = { "${aString}"."${bString}c" = true; }.a.bc;

  recBinds = rec { "${bString}" = a; a = true; }.b;

  multiAttrs = { "${aString}" = true; "${bString}" = false; }.a;
}
