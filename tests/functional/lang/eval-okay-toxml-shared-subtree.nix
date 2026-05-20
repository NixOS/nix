let
  shared = {
    v = 1;
  };
in
builtins.toXML {
  p = shared;
  q = shared;
}
