with import ./lib.nix;

let {

  range = first: last:
    if builtins.lessThan last first
    then []
    else [first] ++ range (builtins.add first 1) last;

  /* Supposedly tail recursive version:

  range_ = accum: first: last:
    if first == last then ([first] ++ accum)
    else range_ ([first] ++ accum) (builtins.add first 1) last;

  range = range_ [];
  */

  x = 12;

  body = sum
    [ (sum (range 1 50))
      (123 + 456)
      (0 + -10 + -(-11) + -x)
    ];
}
