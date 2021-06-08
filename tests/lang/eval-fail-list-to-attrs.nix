# https://github.com/NixOS/nix/issues/4893
builtins.getAttr "nope" (
  builtins.listToAttrs [ { name = "foo"; value = "bar"; } ]
)
