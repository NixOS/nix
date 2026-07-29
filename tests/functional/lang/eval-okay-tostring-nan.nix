# Normalize nan's signbit to avoid portability issues.
let
  inf = 1.0e+300 * 1.0e+300;
  nan = inf - inf;
in
toString [
  nan
  inf
  (-inf)
  (-nan)
]
