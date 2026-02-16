with builtins;
with import <nix/utils.nix>;

let
  showDeprecatedFeature = name: doc: ''
    - [`${name}`](@docroot@/development/deprecated-features.md#dp-feature-${name})
  '';
in
dps: indent "  " (concatStrings (attrValues (mapAttrs showDeprecatedFeature dps)))
