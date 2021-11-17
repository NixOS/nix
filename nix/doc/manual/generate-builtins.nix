builtinsDump:
let
  showBuiltin = name:
    let
      inherit (builtinsDump.${name}) doc args;
    in
    ''
      <dt id="builtins-${name}">
        <a href="#builtins-${name}"><code>${name} ${listArgs args}</code></a>
      </dt>
      <dd>

        ${doc}

      </dd>
    '';
  listArgs = args: builtins.concatStringsSep " " (map (s: "<var>${s}</var>") args);
in
with builtins; concatStringsSep "\n" (map showBuiltin (attrNames builtinsDump))

