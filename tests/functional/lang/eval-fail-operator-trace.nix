# Test that every operator gets a stack frame, even if it's been mangled to a
# builtins call.
let
  a = throw "kaboom";
  b = a.x;
  c = -b;
  d = c ? y;
  e = d ++ [ ];
  f = e * 1;
  g = f / 1;
  h = g - 1;
  i = h + 1;
  j = !i;
  k = j // { };
  l = k < 1;
  m = l <= 1;
  n = m > 1;
  o = n >= 1;
  p = o == 1;
  q = p != 1;
  r = q && a;
  s = r || a;
  t = s -> a;
in
t
