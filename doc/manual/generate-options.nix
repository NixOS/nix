let
  inherit (builtins) attrNames concatStringsSep isAttrs isBool;
  inherit (import ./utils.nix) concatStrings squash splitLines;
in

optionsInfo:
let
  showOption = name:
    let
      inherit (optionsInfo.${name}) description documentDefault defaultValue aliases;
      result = squash ''
          - <span id="conf-${name}">[`${name}`](#conf-${name})</span>

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
          if aliases == [] then "" else
            "**Deprecated alias:** ${(concatStringsSep ", " (map (s: "`${s}`") aliases))}";
      indent = prefix: s:
        concatStringsSep "\n" (map (x: if x == "" then x else "${prefix}${x}") (splitLines s));
      in result;
in concatStrings (map showOption (attrNames optionsInfo))
