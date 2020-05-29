lockFileStr: rootSrc: rootSubdir:

let

  lockFile = builtins.fromJSON lockFileStr;

  allNodes =
    builtins.mapAttrs
      (key: node:
        let
          sourceInfo =
            if key == lockFile.root
            then rootSrc
            else fetchTree (node.info or {} // removeAttrs node.locked ["dir"]);
          subdir = if key == lockFile.root then rootSubdir else node.locked.dir or "";
          flake = import (sourceInfo + (if subdir != "" then "/" else "") + subdir + "/flake.nix");
          inputs = builtins.mapAttrs (inputName: key: allNodes.${key}) (node.inputs or {});
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
