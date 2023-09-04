with builtins;

rec {
  splitLines = s: filter (x: !isList x) (split "\n" s);

  concatStrings = concatStringsSep "";

  replaceStringsRec = from: to: string:
    # recursively replace occurrences of `from` with `to` within `string`
    # example:
    #     replaceStringRec "--" "-" "hello-----world"
    #     => "hello-world"
    let
      replaced = replaceStrings [ from ] [ to ] string;
    in
      if replaced == string then string else replaceStringsRec from to replaced;

  squash = replaceStringsRec "\n\n\n" "\n\n";

  trim = string:
    # trim trailing spaces and squash non-leading spaces
    let
      trimLine = line:
        let
          # separate leading spaces from the rest
          parts = split "(^ *)" line;
          spaces = head (elemAt parts 1);
          rest = elemAt parts 2;
          # drop trailing spaces
          body = head (split " *$" rest);
        in spaces + replaceStringsRec "  " " " body;
    in concatStringsSep "\n" (map trimLine (splitLines string));

  # FIXME: O(n^2)
  unique = foldl' (acc: e: if elem e acc then acc else acc ++ [ e ]) [];

  nameValuePair = name: value: { inherit name value; };

  filterAttrs = pred: set:
    listToAttrs (concatMap (name: let v = set.${name}; in if pred name v then [(nameValuePair name v)] else []) (attrNames set));
}
