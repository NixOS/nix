with builtins;
with import ./utils.nix;

let
  showExperimentalFeature = name: doc:
    ''
      - [`${name}`](@docroot@/contributing/experimental-features/${name}.md)
    '';
in xps: indent "  " (concatStrings (attrValues (mapAttrs showExperimentalFeature xps)))
