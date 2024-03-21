# This is a helper to callFlake() to lazily fetch flake inputs.

# The contents of the lock file, in JSON format.
lockFileStr:

# A mapping of lock file node IDs to { sourceInfo, subdir } attrsets,
# with sourceInfo.outPath providing an InputAccessor to a previously
# fetched tree. This is necessary for possibly unlocked inputs, in
# particular the root input, but also --override-inputs pointing to
# unlocked trees.
overrides:

let

  rawLockFile = builtins.fromJSON lockFileStr;

  # Given a function that returns a string key that sufficiently
  # identifies an attribute value, return an attribute set that
  # returns a unique attribute name for all equivalent attribute values.
  deduplicationMap = f: m:
    builtins.listToAttrs (builtins.attrValues (builtins.mapAttrs
      (name: value: { name = f value; value = name; })
      m
    ));

  # A node is considered duplicate if its `locked` value is
  # equivalent, unless `nodeKey` was overridden.
  nodeDeduplicationKey = { nodeKey, node }:
    if overrides ? ${nodeKey}
    then builtins.toJSON { overriddenAs = nodeKey; }
    else builtins.toJSON node.locked;

  nodeKeyByDeduplicationKey =
    deduplicationMap nodeDeduplicationKey
      (builtins.mapAttrs (nodeKey: node: { inherit nodeKey node; }) rawLockFile.nodes);

  # Resolve an `inputSpec` to a deduplicated root node key.
  deduplicateInputName = inputSpec:
    let
      resolvedInputName = resolveInput inputSpec;
      resolvedInputDeduplicationKey = nodeDeduplicationKey {
        nodeKey = resolvedInputName;
        node = lockFile.nodes.${resolvedInputName};
      };
    in
    nodeKeyByDeduplicationKey.${resolvedInputDeduplicationKey};

  # A `lockFile` with all of its nodes' inputs resolved to their
  # deduplicated equivalents.
  lockFile = rawLockFile // {
      nodes = builtins.mapAttrs
        (key: node: node // {
        inputs = builtins.mapAttrs (_nameForArg: deduplicateInputName) (node.inputs or {});
        })
      rawLockFile.nodes;
  };

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
            if overrides ? ${key}
            then
              overrides.${key}.sourceInfo
            else
              # FIXME: remove obsolete node.info.
              fetchTree (node.info or {} // removeAttrs node.locked ["dir"]);

          subdir = overrides.${key}.dir or node.locked.dir or "";

          outPath = sourceInfo + ((if subdir == "" then "" else "/") + subdir);

          flake = import (outPath + "/flake.nix");

          inputs = builtins.mapAttrs
            (inputName: inputSpec: allNodes.${resolveInput inputSpec})
            (node.inputs or {});

          outputs = flake.outputs (inputs // { self = result; });

          result =
            outputs
            # We add the sourceInfo attribute for its metadata, as they are
            # relevant metadata for the flake. However, the outPath of the
            # sourceInfo does not necessarily match the outPath of the flake,
            # as the flake may be in a subdirectory of a source.
            # This is shadowed in the next //
            // sourceInfo
            // {
              # This shadows the sourceInfo.outPath
              inherit outPath;

              inherit inputs; inherit outputs; inherit sourceInfo; _type = "flake";
            };

        in
          if node.flake or true then
            assert builtins.isFunction flake.outputs;
            result
          else
            sourceInfo
      )
      lockFile.nodes;

in allNodes.${lockFile.root}
