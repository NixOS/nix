let
  shared = {
    v = 1;
  };
in
builtins.toJSON {
  p = shared;
  q = shared;
}
