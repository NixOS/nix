let
  inherit (builtins) attrValues concatStringsSep isAttrs isBool mapAttrs;
  inherit (import <nix/utils.nix>) concatStrings indent optionalString squash;
in

# `inlineHTML` is a hack to accommodate inconsistent output from `lowdown`
{ prefix, inlineHTML ? true }: settingsInfo:

let

  showSetting = prefix: setting: { description, documentDefault, defaultValue, aliases, value, experimentalFeature }:
    let
      result = squash ''
          - ${item}

          ${indent "  " body}
        '';
      item = if inlineHTML
        then ''<span id="${prefix}-${setting}">[`${setting}`](#${prefix}-${setting})</span>''
        else "`${setting}`";
      # separate body to cleanly handle indentation
      body = ''
          ${experimentalFeatureNote}

          ${description}

          **Default:** ${showDefault documentDefault defaultValue}

          ${showAliases aliases}
        '';

      experimentalFeatureNote = optionalString (experimentalFeature != null) ''
          > **Warning**
          >
          > This setting is part of an
          > [experimental feature](@docroot@/contributing/experimental-features.md).
          >
          > To change this setting, make sure the
          > [`${experimentalFeature}` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature})
          > is enabled.
          > For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):
          >
          > ```
          > extra-experimental-features = ${experimentalFeature}
          > ${setting} = ...
          > ```
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
