let
  inherit (builtins) attrNames listToAttrs concatStringsSep readFile replaceStrings;
  inherit (import <nix/utils.nix>) optionalString filterAttrs trim squash toLower unique indent;
  showSettings = import <nix/generate-settings.nix>;
in

{
  # data structure describing all stores and their parameters
  storeInfo,
  # whether to add inline HTML tags
  # `lowdown` does not eat those for one of the output modes
  inlineHTML,
}:

let

  showStore = { name, slug }: { settings, doc, experimentalFeature }:
    let
      result = squash ''
        # ${name}

        ${experimentalFeatureNote}

        ${doc}

        ## Settings

        ${showSettings { prefix = "store-${slug}"; inherit inlineHTML; } settings}
      '';

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

  storesList = map
    (name: rec {
      inherit name;
      slug = replaceStrings [ " " ] [ "-" ] (toLower name);
      filename = "${slug}.md";
      page = showStore { inherit name slug; } storeInfo.${name};
    })
    (attrNames storeInfo);

in storesList
