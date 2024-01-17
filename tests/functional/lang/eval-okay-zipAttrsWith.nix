with import ./lib.nix;

let
  str = builtins.hashString "sha256" "test";
in
builtins.zipAttrsWith
  (n: v: { inherit n v; })
  (map (n: { ${builtins.substring n 1 str} = n; })
    (range 0 31))
