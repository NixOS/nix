xps:

with builtins;
with import ./utils.nix;

let
  makePage = { name, value }:
    {
      name = "${name}.md";
      inherit value;
      feature = name;
    };

  featurePages = map makePage (attrsToList xps);

  tableOfContents = let
    showEntry = page:
      "    - [${page.feature}](contributing/experimental-features/${page.name})";
    in concatStringsSep "\n" (map showEntry featurePages) + "\n";

in (listToAttrs featurePages) // { "SUMMARY.md" = tableOfContents; }
