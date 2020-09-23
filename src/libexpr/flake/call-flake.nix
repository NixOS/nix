with builtins;

lockFileStr: rootSrc: rootSubdir:

let

  lockFile = fromJSON lockFileStr;

  allNodes =
    mapAttrs
      (key: node:
        let

          sourceInfo =
            if key == lockFile.root
            then rootSrc
            else fetchTree (node.info or {} // removeAttrs node.locked ["dir"]);

          subdir = if key == lockFile.root then rootSubdir else node.locked.dir or "";

          flakeDir = sourceInfo + (if subdir != "" then "/" else "") + subdir;

          flake =
            if pathExists (flakeDir + "/flake.nix")
            then import (flakeDir + "/flake.nix")
            else if pathExists (flakeDir + "/nix.toml")
            then
              # Convert nix.toml to a flake containing a 'modules'
              # output.
              let
                toml = fromTOML (readFile (flakeDir + "/nix.toml"));
              in {
                inputs = toml.inputs or {};
                outputs = inputs: {
                  modules =
                    listToAttrs (
                      map (moduleName:
                        let
                          m = toml.${moduleName};
                        in {
                          name = moduleName;
                          value = module {
                            extends =
                              map (flakeRef:
                                let
                                  tokens = match ''(.*)#(.*)'' flakeRef;
                                in
                                  assert tokens != null;
                                  inputs.${elemAt tokens 0}.modules.${elemAt tokens 1}
                              ) (m.extends or []);
                            config = { config }: listToAttrs (map
                              (optionName:
                                { name = optionName;
                                  value = m.${optionName};
                                }
                              )
                              (filter
                                (n: n != "extends" && n != "doc")
                                (attrNames m)));
                          };
                        })
                        (filter
                          (n: isAttrs toml.${n} && n != "inputs")
                          (attrNames toml)));
                };
              }
            else throw "flake does not contain a 'flake.nix' or 'nix.toml'";

          inputs = mapAttrs
            (inputName: inputSpec: allNodes.${resolveInput inputSpec})
            (node.inputs or {});

          # Resolve a input spec into a node name. An input spec is
          # either a node name, or a 'follows' path from the root
          # node.
          resolveInput = inputSpec:
              if isList inputSpec
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
                (resolveInput lockFile.nodes.${nodeName}.inputs.${head path})
                (tail path);

          outputs = flake.outputs (inputs // { self = result; });

          result = outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; };
        in
          if node.flake or true then
            assert isFunction flake.outputs;
            result
          else
            sourceInfo
      )
      lockFile.nodes;

in allNodes.${lockFile.root}
