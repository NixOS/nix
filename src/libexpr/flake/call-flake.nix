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

          outPath = sourceInfo + ((if subdir == "" then "" else "/") + subdir);

          flake = import (outPath + "/flake.nix");

          inputs = builtins.mapAttrs
            (inputName: inputSpec: allNodes.${resolveInput inputSpec})
            (node.inputs or {});

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

          # Attributes that are added to the final representation of the flake.
          # NB: `attrNames extraAttributes` must be lazy in `outputs` (tested). Values may depend on `outputs`.
          extraAttributes =
            sourceInfo
            // {
              # This shadows the sourceInfo.outPath
              inherit outPath;

              inherit inputs; inherit outputs; inherit sourceInfo; _type = "flake";
            };

          meta = {
            # The source root, which may not correspond to the flake directory.
            inherit sourceInfo;
            # The base directory of the flake
            inherit subdir;
            # Extra attributes in the final representation of the flake, added to result of the output function
            inherit extraAttributes;
            # Extra inputs to the output function
            inherit extraArguments;
          };

          # NB: `attrNames arguments` must be lazy in `outputs` (tested).
          arguments = inputs // extraArguments;

          extraArguments = {
            self = result;
            inherit meta;
          };

          outputs = flake.outputs arguments;

          result =
            outputs
            # We add the sourceInfo attribute for its metadata, as they are
            # relevant metadata for the flake. However, the outPath of the
            # sourceInfo does not necessarily match the outPath of the flake,
            # as the flake may be in a subdirectory of a source.
            # This is shadowed in the next //
            // extraAttributes;

        in
          if node.flake or true then
            assert builtins.isFunction flake.outputs;
            result
          else
            sourceInfo
      )
      lockFile.nodes;

in allNodes.${lockFile.root}
