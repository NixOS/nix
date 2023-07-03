let
  inherit (builtins) concatStringsSep attrValues mapAttrs;
  inherit (import ./utils.nix) optionalString squash;
in

builtinsInfo:
let
  showBuiltin = name: { doc, type, impure-only }:
    let
      type' = optionalString (type != null) " (${type})";

      impureNotice = optionalString impure-only ''
        Not available in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).
      '';
    in
    squash ''
      <dt id="builtin-constants-${name}">
        <a href="#builtin-constants-${name}"><code>${name}</code>${type'}</a>
      </dt>
      <dd>

      ${doc}

      ${impureNotice}

      </dd>
    '';
in
concatStringsSep "\n" (attrValues (mapAttrs showBuiltin builtinsInfo))
