with builtins;

[ (concatStringsSep "" [])
  (concatStringsSep "" ["foo" "bar" "xyzzy"])
  (concatStringsSep ", " ["foo" "bar" "xyzzy"])
  (concatStringsSep ", " ["foo"])
  (concatStringsSep ", " [])
]
