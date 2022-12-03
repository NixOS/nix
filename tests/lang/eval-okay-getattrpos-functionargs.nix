let
  fun = { foo }: {};
  pos = builtins.tryGetAttrPos "foo" (builtins.functionArgs fun);
in { inherit (pos) column line; file = baseNameOf pos.file; }
