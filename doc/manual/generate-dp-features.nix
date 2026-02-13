with builtins;
with import <nix/utils.nix>;

let
  showDeprecatedFeature =
    name: doc:
    squash ''
      ## [`${name}`]{#dp-feature-${name}}

      ${doc}
    '';
in

dps: (concatStringsSep "\n" (attrValues (mapAttrs showDeprecatedFeature dps)))
