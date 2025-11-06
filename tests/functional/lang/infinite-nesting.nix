let
  mkInfinite = i: { "a${toString i}" = mkInfinite (i + 1); };
in
mkInfinite 0
