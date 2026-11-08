builtins.toXML [
  ("a" + "b")
  10
  (rec {
    x = "x";
    y = x;
  })
  <nix/fetchurl.nix>
  <nix/some/other/path>
  (/. + builtins.storeDir)
  (/. + builtins.unsafeDiscardStringContext (builtins.toFile "test" "something"))
]
