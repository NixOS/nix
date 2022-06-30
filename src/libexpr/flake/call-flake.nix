lockFileStr: rootSrc: rootSubdir:

let

  lockFile = builtins.fromJSON lockFileStr;

  # Resolve a input spec into a node name. An input spec is
  # either a node name, or a 'follows' path from the root
  # node.
  resolveInput = inputSpec:
    if builtins.isList inputSpec
    then getInputByPath lockFile.root inputSpec
    else inputSpec;

  # Follow an input path (e.g. ["dwarffs" "nixpkgs"]) from the
  # root node, returning the final node.
  getInputByPath = nodeName: path:
    if path == []
    then nodeName
    else
      getInputByPath
        # Since this could be a 'follows' input, call resolveInput.
        (resolveInput lockFile.nodes.${nodeName}.inputs.${builtins.head path})
        (builtins.tail path);

  allNodes =
    builtins.mapAttrs
      (key: node:
        let

          sourceInfo =
            if key == lockFile.root
            then rootSrc
            else if node.locked.type == "path" && builtins.substring 0 1 node.locked.path != "/"
            then
              let
                parentNode = allNodes.${getInputByPath lockFile.root node.parent};
              in parentNode.sourceInfo // {
                outPath = parentNode.sourceInfo.outPath + ("/" + node.locked.path);
              }
            else
              # FIXME: remove obsolete node.info.
              fetchTree (node.info or {} // removeAttrs node.locked ["dir"]);

          subdir = if key == lockFile.root then rootSubdir else node.locked.dir or "";

          flake =
            import (sourceInfo.outPath + ((if subdir != "" then "/" else "") + subdir + "/flake.nix"));

          inputs = builtins.mapAttrs
            (inputName: inputSpec: allNodes.${resolveInput inputSpec})
            (node.inputs or {});

          outputs = flake.outputs (inputs // { self = result; });

          result = outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; };
        in
          if node.flake or true then
            assert builtins.isFunction flake.outputs;
            result
          else
            sourceInfo
      )
      lockFile.nodes;

in allNodes.${lockFile.root}
