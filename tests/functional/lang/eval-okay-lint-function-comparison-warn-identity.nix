# Pointer equality on a value with no functions: no warning expected.
let
  a = {
    x = 1;
  };
in
a == a
