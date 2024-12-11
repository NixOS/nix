{ name, uidRange ? false }:

with import <nixpkgs> {};

runCommand name
  { requiredSystemFeatures = if uidRange then ["uid-range"] else [];
  }
  "id; id > $out"
