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

  body = sum [
    (sum (range 1 50))
    (123 + 456)
    (011) # Literals with leading zeros are fine and are always decimal.
    (0 + -10 + -(-11) + -x)
    (10 - 7 - -2)
    (10 - (6 - -1))
    (10 - 1 + 2)
    (3 * 4 * 5)
    (56088 / 123 / 2)
    (3 + 4 * const 5 0 - 6 / id 2)

    (builtins.bitAnd 12 10) # 0b1100 & 0b1010 =  8
    (builtins.bitOr 12 10) # 0b1100 | 0b1010 = 14
    (builtins.bitXor 12 10) # 0b1100 ^ 0b1010 =  6

    # It's possible to override operators.
    ( let __sub = builtins.add; in 1 - 2 )
    ( let __mul = builtins.div; in 2 * 2 )
    ( let __div = builtins.mul; in 2 / 2 )
    ( let __lessThan = x: y: builtins.lessThan y x; in if 3 < 7 then err else 1 )

    (if 3 < 7 then 1 else err)
    (if 7 < 3 then err else 1)
    (if 3 < 3 then err else 1)

    (if 3 <= 7 then 1 else err)
    (if 7 <= 3 then err else 1)
    (if 3 <= 3 then 1 else err)

    (if 3 > 7 then err else 1)
    (if 7 > 3 then 1 else err)
    (if 3 > 3 then err else 1)

    (if 3 >= 7 then err else 1)
    (if 7 >= 3 then 1 else err)
    (if 3 >= 3 then 1 else err)

    (if 2 > 1 == 1 < 2 then 1 else err)
    (if 1 + 2 * 3 >= 7 then 1 else err)
    (if 1 + 2 * 3 < 7 then err else 1)

    # Not integer, but so what.
    (if "aa" < "ab" then 1 else err)
    (if "aa" < "aa" then err else 1)
    (if "foo" < "foobar" then 1 else err)
  ];

}
