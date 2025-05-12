# This is a helper to callFlake() to lazily fetch flake inputs.

# The contents of the lock file, in JSON format.
lockFileStr:

# A mapping of lock file node IDs to { sourceInfo, subdir } attrsets,
# with sourceInfo.outPath providing an SourceAccessor to a previously
# fetched tree. This is necessary for possibly unlocked inputs, in
# particular the root input, but also --override-inputs pointing to
# unlocked trees.
overrides:

# This is `prim_fetchFinalTree`.
fetchTreeFinal:

let
  inherit (builtins) mapAttrs;

  lockFile = builtins.fromJSON lockFileStr;

  # Resolve a input spec into a node name. An input spec is
  # either a node name, or a 'follows' path from the root
  # node.
  resolveInput =
    inputSpec: if builtins.isList inputSpec then getInputByPath lockFile.root inputSpec else inputSpec;

  # Follow an input attrpath (e.g. ["dwarffs" "nixpkgs"]) from the
  # root node, returning the final node.
  getInputByPath =
    nodeName: path:
    if path == [ ] then
      nodeName
    else
      getInputByPath
        # Since this could be a 'follows' input, call resolveInput.
        (resolveInput lockFile.nodes.${nodeName}.inputs.${builtins.head path})
        (builtins.tail path);

  allNodes = mapAttrs (
    key: node:
    let
      hasOverride = overrides ? ${key};
      isRelative = node.locked.type or null == "path" && builtins.substring 0 1 node.locked.path != "/";

      parentNode = allNodes.${getInputByPath lockFile.root node.parent};

      sourceInfo =
        if hasOverride then
          overrides.${key}.sourceInfo
        else if isRelative then
          parentNode.sourceInfo
        else
          # FIXME: remove obsolete node.info.
          # Note: lock file entries are always final.
          fetchTreeFinal (node.info or { } // removeAttrs node.locked [ "dir" ]);

      subdir = overrides.${key}.dir or node.locked.dir or "";

      outPath =
        if !hasOverride && isRelative then
          parentNode.outPath + (if node.locked.path == "" then "" else "/" + node.locked.path)
        else
          sourceInfo.outPath + (if subdir == "" then "" else "/" + subdir);

      flake = import (outPath + "/flake.nix");

      inputs = mapAttrs (inputName: inputSpec: allNodes.${resolveInput inputSpec}.result) (
        node.inputs or { }
      );

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

          inherit inputs;
          inherit outputs;
          inherit sourceInfo;
          _type = "flake";
        };

    in
    {
      result =
        if node.flake or true then
          assert builtins.isFunction flake.outputs;
          result
        else
          sourceInfo // { inherit sourceInfo outPath; };

      inherit outPath sourceInfo;
    }
  ) lockFile.nodes;

in
allNodes.${lockFile.root}.result
