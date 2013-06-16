let
  inherit (builtins) length elemAt add isAttrs head tail getAttr
    hasAttr filter attrNames lessThan listToAttrs sub;

  foldl = op: nul: list:
    let
      minus1 = dec 0;

      foldl' = n:
        if n == minus1
        then nul
        else op (foldl' (dec n)) (elemAt list n);
    in foldl' (dec (length list));

  flip = f: a: b: f b a;

  dec = flip sub 1;

  inc = add 1;

  safeHasAttr = name: x: isAttrs x && hasAttr name x;

  # Can't foldl because we need to exclude the last element
  getAttrsExceptLast = attrs: names:
    let
      len = dec (length names);

      get = attrs: n: let name = elemAt names n; in
        if n == len
          then attrs
          else if safeHasAttr name attrs
            then get (getAttr name attrs) (inc n)
            else null;
    in get attrs 0;

  last = l: elemAt l (dec (length l));
in {
  getAttrs = foldl (flip getAttr);

  hasAttrs = attrs: names:
    safeHasAttr (last names) (getAttrsExceptLast attrs names);

  getAttrsOr = def: attrs: names:
    let
      lastValue = getAttrsExceptLast attrs names;

      lastName = last names;
    in if safeHasAttr lastName lastValue then getAttr lastName lastValue else def;

  bindAttrs = values: positions: dynamicBindings: (foldl ({positions, values}: { names, value, position }:
    let
      len = length names;

      foundPositions =
        let
          firstAttr = attrs: getAttr (head (attrNames attrs)) attrs;

          getPos = pos: if isAttrs pos then getPos (firstAttr pos) else pos;

          found = positions: n: let name = elemAt names n; in
            if n == len
              then { positions = getPos positions; }
              else if safeHasAttr name positions
                then found (getAttr name positions) (inc n)
                else { depth = n; inherit positions; };
        in found positions 0;

      foundDepth = if isAttrs foundPositions.positions
        then foundPositions.depth
        else abort "Attribute `${
          foldl (str: name: str + ".${name}") (head names) (tail names)
        }' at ${position} already defined at ${foundPositions.positions}";

      updateAttrs = set: value:
        let
          update = set: n: let name = elemAt names n; in
            set // listToAttrs [{
              inherit name;

              value = if n == foundDepth
                then let set' = (n: let name = elemAt names n; in
                  if n == len
                    then value
                    else listToAttrs [ { inherit name; value = set' (inc n); } ]
                ); in set' (inc n)
                else update (getAttr name set) (inc n);
            }];
        in update set 0;
    in {
      positions = updateAttrs positions position;
      values = updateAttrs values value;
    }
  ) { inherit positions values; } dynamicBindings).values;
}
