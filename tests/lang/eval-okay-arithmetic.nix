with import ./lib.nix;

let {

  range = first: last: [first] ++ (if first == last then [] else range (builtins.add first 1) last);

  /* Supposedly tail recursive version:
  
  range_ = accum: first: last: 
    if first == last then ([first] ++ accum)
    else range_ ([first] ++ accum) (builtins.add first 1) last;

  range = range_ [];
  */

  body = sum (range 1 50);

}
