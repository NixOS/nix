let
  inherit (builtins)
    attrValues
    concatStringsSep
    isAttrs
    isBool
    mapAttrs
    ;
  inherit (import <nix/utils.nix>)
    concatStrings
    indent
    optionalString
    squash
    ;
in

# `inlineHTML` is a hack to accommodate inconsistent output from `lowdown`
{
  prefix,
  inlineHTML ? true,
}:
settingsInfo:

let

  showSetting =
    prefix: setting:
    {
      description,

      # Whether we document the default, because it is machine agostic,
      # or don't because because it is machine-specific.
      documentDefault,

      # The default value is JSON for new-style config, rather than then
      # a string or boolean, for old-style config.
      json ? false,

      defaultValue ? null,

      aliases ? [ ],

      # The current value for this setting. Purposefully unused.
      value ? null,
      experimentalFeature ? null,
    }:
    let
      result = squash ''
        - ${item}

        ${indent "  " body}
      '';
      item =
        if inlineHTML then
          ''<span id="${prefix}-${setting}">[`${setting}`](#${prefix}-${setting})</span>''
        else
          "`${setting}`";
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
        > [experimental feature](@docroot@/development/experimental-features.md).
        >
        > To change this setting, make sure the
        > [`${experimentalFeature}` experimental feature](@docroot@/development/experimental-features.md#xp-feature-${experimentalFeature})
        > is enabled.
        > For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):
        >
        > ```
        > extra-experimental-features = ${experimentalFeature}
        > ${setting} = ...
        > ```
      '';

      showDefault =
        documentDefault: defaultValue:
        if documentDefault then
          if json then
            "`${builtins.toJSON defaultValue}`"
          else
          # a StringMap value type is specified as a string, but
          # this shows the value type. The empty stringmap is `null` in
          # JSON, but that converts to `{ }` here.
          if defaultValue == "" || defaultValue == [ ] || isAttrs defaultValue then
            "*empty*"
          else if isBool defaultValue then
            if defaultValue then "`true`" else "`false`"
          else
            "`${toString defaultValue}`"
        else
          "*machine-specific*";

      showAliases =
        aliases:
        optionalString (aliases != [ ])
          "**Deprecated alias:** ${(concatStringsSep ", " (map (s: "`${s}`") aliases))}";

    in
    result;

in
concatStrings (attrValues (mapAttrs (showSetting prefix) settingsInfo))
