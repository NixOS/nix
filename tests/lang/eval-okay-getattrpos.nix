let
  as = {
    foo = "bar";
  };
  pos = builtins.tryGetAttrPos "foo" as;
in { inherit (pos) column line; file = baseNameOf pos.file; }
