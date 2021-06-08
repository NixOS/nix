builtins.getAttr "nope" (
  builtins.listToAttrs [ { name = "foo"; value = "bar"; } ]
)
