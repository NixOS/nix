with builtins;

rec {
  splitLines = s: filter (x: !isList x) (split "\n" s);

  concatStrings = concatStringsSep "";

  # FIXME: O(n^2)
  unique = foldl' (acc: e: if elem e acc then acc else acc ++ [ e ]) [];

  nameValuePair = name: value: { inherit name value; };

  filterAttrs = pred: set:
    listToAttrs (concatMap (name: let v = set.${name}; in if pred name v then [(nameValuePair name v)] else []) (attrNames set));
}
