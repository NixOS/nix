let
  a = {
    b = a;
  };
in
builtins.toXML a
