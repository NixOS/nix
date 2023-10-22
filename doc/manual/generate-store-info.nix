let
  inherit (builtins) attrValues mapAttrs;
  inherit (import ./utils.nix) concatStrings optionalString;
  showSettings = import ./generate-settings.nix;
in

inlineHTML: storesInfo:

let

  showStore = name: { settings, doc, experimentalFeature }:
    let

    result = ''
      ## ${name}

      ${doc}

      ${experimentalFeatureNote}

      ### Settings

      ${showSettings { prefix = "store-${slug}"; inherit inlineHTML; } settings}
    '';

      # markdown doesn't like spaces in URLs
      slug = builtins.replaceStrings [ " " ] [ "-" ] name;

    experimentalFeatureNote = optionalString (experimentalFeature != null) ''
      > **Warning**
      > This store is part of an
      > [experimental feature](@docroot@/contributing/experimental-features.md).

      To use this store, you need to make sure the corresponding experimental feature,
      [`${experimentalFeature}`](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature}),
      is enabled.
      For example, include the following in [`nix.conf`](#):

      ```
      extra-experimental-features = ${experimentalFeature}
      ```
    '';
  in result;

in concatStrings (attrValues (mapAttrs showStore storesInfo))
