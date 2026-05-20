let
  a = {
    x = b;
  };
  b = {
    y = a;
  };
in
builtins.toJSON a
