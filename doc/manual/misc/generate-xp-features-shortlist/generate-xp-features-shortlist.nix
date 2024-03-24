with builtins;
with import <nix/utils.nix>;

let
  showExperimentalFeature = name: doc:
    ''
      - [`${name}`](@docroot@/contributing/experimental-features.md#xp-feature-${name})
    '';
in xps: indent "  " (concatStrings (attrValues (mapAttrs showExperimentalFeature xps)))
