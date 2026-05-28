let
  a = {
    x = b;
  };
  b = {
    y = a;
  };
in
builtins.toXML a
