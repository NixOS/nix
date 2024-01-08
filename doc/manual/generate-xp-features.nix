with builtins;
with import <nix/utils.nix>;

let
  showExperimentalFeature = name: doc:
    squash ''
      ## [`${name}`]{#xp-feature-${name}}

      ${doc}
    '';
in

xps: (concatStringsSep "\n" (attrValues (mapAttrs showExperimentalFeature xps)))
