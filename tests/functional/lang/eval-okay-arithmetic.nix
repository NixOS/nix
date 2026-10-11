with import ./lib.nix;

let {

  /*
    Supposedly tail recursive version:

    range_ = accum: first: last:
      if first == last then ([first] ++ accum)
      else range_ ([first] ++ accum) (builtins.add first 1) last;

    range = range_ [];
  */

  x = 12;

  err = abort "urgh";

  body = {
    sumRange = sum (range 1 50);
    add = 123 + 456;
    leadingZero = 011; # Literals with leading zeros are fine and are always decimal.
    negation = 0 + -10 + -(-11) + -x;
    subLeftAssoc = 10 - 7 - -2;
    subParens = 10 - (6 - -1);
    subAdd = 10 - 1 + 2;
    mul = 3 * 4 * 5;
    divLeftAssoc = 56088 / 123 / 2;
    precedence = 3 + 4 * const 5 0 - 6 / id 2;

    bitAnd = builtins.bitAnd 12 10; # 0b1100 & 0b1010 =  8
    bitOr = builtins.bitOr 12 10; # 0b1100 | 0b1010 = 14
    bitXor = builtins.bitXor 12 10; # 0b1100 ^ 0b1010 =  6

    # It's possible to override operators.
    overrideSub =
      let
        __sub = builtins.add;
      in
      1 - 2;
    overrideMul =
      let
        __mul = builtins.div;
      in
      2 * 2;
    overrideDiv =
      let
        __div = builtins.mul;
      in
      2 / 2;
    overrideLessThan =
      let
        __lessThan = x: y: builtins.lessThan y x;
      in
      if 3 < 7 then err else 1;

    lt_3_7 = if 3 < 7 then 1 else err;
    lt_7_3 = if 7 < 3 then err else 1;
    lt_3_3 = if 3 < 3 then err else 1;

    le_3_7 = if 3 <= 7 then 1 else err;
    le_7_3 = if 7 <= 3 then err else 1;
    le_3_3 = if 3 <= 3 then 1 else err;

    gt_3_7 = if 3 > 7 then err else 1;
    gt_7_3 = if 7 > 3 then 1 else err;
    gt_3_3 = if 3 > 3 then err else 1;

    ge_3_7 = if 3 >= 7 then err else 1;
    ge_7_3 = if 7 >= 3 then 1 else err;
    ge_3_3 = if 3 >= 3 then 1 else err;

    precedenceEq = if 2 > 1 == 1 < 2 then 1 else err;
    precedenceGe = if 1 + 2 * 3 >= 7 then 1 else err;
    precedenceLt = if 1 + 2 * 3 < 7 then err else 1;

    # Not integer, but so what.
    stringLt = if "aa" < "ab" then 1 else err;
    stringLtEqual = if "aa" < "aa" then err else 1;
    stringLtPrefix = if "foo" < "foobar" then 1 else err;
  };

}
