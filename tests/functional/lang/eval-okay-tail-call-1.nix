let
  f = n: if n == 100000 then n else f (n + 1);
in f 0
