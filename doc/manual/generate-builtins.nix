let
  inherit (builtins) concatStringsSep attrValues mapAttrs;
  inherit (import ./utils.nix) optionalString squash;
in

builtinsInfo:
let
  showBuiltin = name: { doc, args, arity, experimental-feature }:
    let
      experimentalNotice = optionalString (experimental-feature != null) ''
        This function is only available if the [${experimental-feature}](@docroot@/contributing/experimental-features.md#xp-feature-${experimental-feature}) experimental feature is enabled.
      '';
    in
    squash ''
      <dt id="builtins-${name}">
        <a href="#builtins-${name}"><code>${name} ${listArgs args}</code></a>
      </dt>
      <dd>

      ${doc}

      ${experimentalNotice}

      </dd>
    '';
  listArgs = args: concatStringsSep " " (map (s: "<var>${s}</var>") args);
in
concatStringsSep "\n" (attrValues (mapAttrs showBuiltin builtinsInfo))
