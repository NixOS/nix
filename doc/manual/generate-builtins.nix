let
  inherit (builtins) concatStringsSep attrNames;
in

builtinsInfo:
let
  showBuiltin = name:
    let
      inherit (builtinsInfo.${name}) doc args;
    in
    ''
      <dt id="builtins-${name}">
        <a href="#builtins-${name}"><code>${name} ${listArgs args}</code></a>
      </dt>
      <dd>

        ${doc}

      </dd>
    '';
  listArgs = args: concatStringsSep " " (map (s: "<var>${s}</var>") args);
in
concatStringsSep "\n" (map showBuiltin (attrNames builtinsInfo))

