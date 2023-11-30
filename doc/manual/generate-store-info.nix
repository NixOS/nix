let
  inherit (builtins) attrValues mapAttrs;
  inherit (import <nix/utils.nix>) concatStrings optionalString squash;
  showSettings = import <nix/generate-settings.nix>;
in

inlineHTML: storesInfo:

let

  showStore = name: { settings, doc, experimentalFeature }:
    let
      result = squash ''
        # ${name}

        ${doc}

        ${experimentalFeatureNote}

        ## Settings

        ${showSettings { prefix = "store-${slug}"; inherit inlineHTML; } settings}
      '';

      # markdown doesn't like spaces in URLs
      slug = builtins.replaceStrings [ " " ] [ "-" ] name;

      experimentalFeatureNote = optionalString (experimentalFeature != null) ''
        > **Warning**
        >
        > This store is part of an
        > [experimental feature](@docroot@/contributing/experimental-features.md).
        >
        > To use this store, make sure the
        > [`${experimentalFeature}` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature})
        > is enabled.
        > For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):
        >
        > ```
        > extra-experimental-features = ${experimentalFeature}
        > ```
      '';
    in result;

in concatStrings (attrValues (mapAttrs showStore storesInfo))
