let
  fun = { foo }: {};
  pos = builtins.unsafeGetLambdaPos fun;
in { inherit (pos) column line; file = baseNameOf pos.file; }
