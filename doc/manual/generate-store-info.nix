let
  inherit (builtins) attrNames listToAttrs concatStringsSep;
  inherit (import <nix/utils.nix>) optionalString;
  showSettings = import <nix/generate-settings.nix>;
in

{ inlineHTML, stores, headingLevel ? "#" }:

let

  showStore = { name, slug }: { settings, doc, experimentalFeature }:
    let
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
    in ''
      ${headingLevel} ${name}

      ${doc}

      ${experimentalFeatureNote}

      ${headingLevel}# Settings

      ${showSettings { prefix = "store-${slug}"; inherit inlineHTML; } settings}
    '';

  storesList = map
    (name: rec {
      inherit name;
      # markdown doesn't like spaces in URLs
      slug = builtins.replaceStrings [ " " ] [ "-" ] name;
      filename = "${slug}.md";
      page = showStore { inherit name slug; } stores.${name};
    })
    (attrNames stores);

  tableOfContents = let
    showEntry = store:
      "    - [${store.name}](store-types/${store.filename})";
    in concatStringsSep "\n" (map showEntry storesList) + "\n";

in listToAttrs (map (s: { name = s.filename; value = s.page; }) storesList) // { "SUMMARY.md" = tableOfContents; }
