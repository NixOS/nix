let
  increment = x: x + 1;

  # this part is ok
  P = increment Q;
  Q = increment R;
  R = increment a;

  # `a` is where it loops back to
  a = increment b;
  b = increment c;
  c = increment d;
  d = increment a;

in P
