with builtins;

rec {
  splitLines = s: filter (x: !isList x) (split "\n" s);

  concatStrings = concatStringsSep "";

  attrsToList = a:
    map (name: { inherit name; value = a.${name}; }) (builtins.attrNames a);

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

  optionalString = cond: string: if cond then string else "";

  showSetting = { useAnchors }: name: { description, documentDefault, defaultValue, aliases, value }:
    let
      result = squash ''
          - ${if useAnchors
              then ''<span id="conf-${name}">[`${name}`](#conf-${name})</span>''
              else ''`${name}`''}

          ${indent "  " body}
        '';

      # separate body to cleanly handle indentation
      body = ''
          ${description}

          **Default:** ${showDefault documentDefault defaultValue}

          ${showAliases aliases}
        '';

      showDefault = documentDefault: defaultValue:
        if documentDefault then
          # a StringMap value type is specified as a string, but
          # this shows the value type. The empty stringmap is `null` in
          # JSON, but that converts to `{ }` here.
          if defaultValue == "" || defaultValue == [] || isAttrs defaultValue
            then "*empty*"
            else if isBool defaultValue then
              if defaultValue then "`true`" else "`false`"
            else "`${toString defaultValue}`"
        else "*machine-specific*";

      showAliases = aliases:
          optionalString (aliases != [])
            "**Deprecated alias:** ${(concatStringsSep ", " (map (s: "`${s}`") aliases))}";

    in result;

  indent = prefix: s:
    concatStringsSep "\n" (map (x: if x == "" then x else "${prefix}${x}") (splitLines s));

  showSettings = args: settingsInfo: concatStrings (attrValues (mapAttrs (showSetting args) settingsInfo));
}
