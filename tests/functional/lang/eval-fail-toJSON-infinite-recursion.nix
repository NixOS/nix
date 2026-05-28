let
  a = {
    b = a;
  };
in
builtins.toJSON a
