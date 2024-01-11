let
  as = {
    foo = "bar";
  };
  pos = builtins.unsafeGetAttrPos "foo" as;
in { inherit (pos) column line; file = baseNameOf pos.file; }
