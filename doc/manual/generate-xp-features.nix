with builtins;
with import ./utils.nix;

let
  showExperimentalFeature = name: doc:
    squash ''
      - <span id="xp-feature-${name}">[`${name}`](#xp-feature-${name})</span>

      ${indent "  " doc}
    '';
in xps: indent "  " (concatStringsSep "\n" (attrValues (mapAttrs showExperimentalFeature xps)))
