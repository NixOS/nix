let
  sixteenBytes = "0123456789abcdef";
  times16 = s: builtins.concatStringsSep "" [s s s s s s s s s s s s s s s s];
  exp = n: x: if n == 1 then x else times16 (exp (n - 1) x);
  sixteenMegabyte = exp 6 sixteenBytes;
in
assert builtins.stringLength sixteenMegabyte == 16777216;
derivation {
  name = "big-derivation-attr";
  builder = "/x";
  system = "y";
  bigAttr = sixteenMegabyte;
}
