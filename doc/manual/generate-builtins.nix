let
  inherit (builtins) concatStringsSep attrValues mapAttrs;
  inherit (import <nix/utils.nix>) optionalString squash;
in

builtinsInfo:
let
  showBuiltin = name: { doc, args, arity, experimental-feature }:
    let
      experimentalNotice = optionalString (experimental-feature != null) ''
        > **Note**
        >
        > This function is only available if the [`${experimental-feature}` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-${experimental-feature}) is enabled.
        >
        > For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):
        >
        > ```
        > extra-experimental-features = ${experimental-feature}
        > ```
      '';
    in
    squash ''
      <dt id="builtins-${name}">
        <a href="#builtins-${name}"><code>${name} ${listArgs args}</code></a>
      </dt>
      <dd>

      ${experimentalNotice}

      ${doc}
      </dd>
    '';
  listArgs = args: concatStringsSep " " (map (s: "<var>${s}</var>") args);
in
concatStringsSep "\n" (attrValues (mapAttrs showBuiltin builtinsInfo))
