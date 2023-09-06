let
  inherit (builtins) attrValues concatStringsSep isAttrs isBool mapAttrs;
  inherit (import ./utils.nix) concatStrings indent optionalString squash;
in

prefix: settingsInfo:

let

  showSetting = prefix: setting: { description, documentDefault, defaultValue, aliases, value, experimentalFeature }:
    let
      result = squash ''
          - <span id="${prefix}-${setting}">[`${setting}`](#${prefix}-${setting})</span>

          ${indent "  " body}
        '';

      # separate body to cleanly handle indentation
      body = ''
          ${description}

          ${experimentalFeatureNote}

          **Default:** ${showDefault documentDefault defaultValue}

          ${showAliases aliases}
        '';

      experimentalFeatureNote = optionalString (experimentalFeature != null) ''
          > **Warning**
          > This setting is part of an
          > [experimental feature](@docroot@/contributing/experimental-features.md).

          To change this setting, you need to make sure the corresponding experimental feature,
          [`${experimentalFeature}`](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature}),
          is enabled.
          For example, include the following in [`nix.conf`](#):

          ```
          extra-experimental-features = ${experimentalFeature}
          ${setting} = ...
          ```
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

in concatStrings (attrValues (mapAttrs (showSetting prefix) settingsInfo))
