let
  inf = 1.0e+200 * 1.0e+200;
  neginf = -1.0e+200 * 1.0e+200;
  # On x86 the canonical quiet NaN (as produced by e.g. inf / inf) has a negative sign.
  stripNanSign =
    nan:
    let
      s = toString nan;
    in
    if (builtins.substring 0 1) s == "-" then (builtins.substring 1 (-1) s) else s;
  # Canonincal NaN has the same sign regardless of the operand's own signs.
  nan = inf / inf;
  nan' = inf / neginf;
in
[
  (1.1 + 2.3)
  (builtins.add (0.5 + 0.5) (2.0 + 0.5))
  ((0.5 + 0.5) * (2.0 + 0.5))
  ((1.5 + 1.5) / (0.5 * 4.0))
  # Various literals
  1.123456789e1
  1.3e-1
  6.7e+1
  6.78901234E-1
  .3e-1
  0.3456e-1
  0.3456e+1
  0.3456e+01
  0.3456e+02
  3.456e-01
  3.456E-001
  123.456789E+02
  inf
  neginf
  (inf * neginf) # -inf
  (neginf * inf) # -inf
  (neginf * neginf) # inf
  (inf * inf) # inf
  (stripNanSign nan)
  (stripNanSign nan')
]
