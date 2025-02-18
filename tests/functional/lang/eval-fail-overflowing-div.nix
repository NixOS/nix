let
  # lol, this has to be written as an expression like this because negative
  # numbers use unary negation rather than parsing directly, and 2**63 is out
  # of range
  intMin = -9223372036854775807 - 1;
  b = -1;
in
builtins.seq intMin (builtins.seq b (intMin / b))
