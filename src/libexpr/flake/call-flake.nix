lockFileStr: rootSrc: rootSubdir:

let
  inherit (builtins) listToAttrs concatMap attrNames mapAttrs isAttrs isList foldl';
  optionalAttrs = check: value: if check then value else { };

  mergeAny = lhs: rhs:
    lhs // mapAttrs
      (name: value:
        if isAttrs value then lhs.${name} or { } // value
        else if isList value then lhs.${name} or [ ] ++ value
        else value
      )
      rhs;

  eachSystem = systems: f:
    let
      # Taken from <nixpkgs/lib/attrsets.nix>
      isDerivation = x: builtins.isAttrs x && x ? type && x.type == "derivation";

      # Used to match Hydra's convention of how to define jobs. Basically transforms
      #
      #     hydraJobs = {
      #       hello = <derivation>;
      #       haskellPackages.aeson = <derivation>;
      #     }
      #
      # to
      #
      #     hydraJobs = {
      #       hello.x86_64-linux = <derivation>;
      #       haskellPackages.aeson.x86_64-linux = <derivation>;
      #     }
      #
      # if the given flake does `eachSystem [ "x86_64-linux" ] { ... }`.
      pushDownSystem = system: merged:
        builtins.mapAttrs
          (name: value:
            if ! (builtins.isAttrs value) then value
            else if isDerivation value then (merged.${name} or { }) // { ${system} = value; }
            else pushDownSystem system (merged.${name} or { }) value);

      # Merge together the outputs for all systems.
      op = attrs: system:
        let
          ret = f system;
          op = attrs: key:
            let
              appendSystem = key: system: ret:
                if key == "hydraJobs"
                then (pushDownSystem system (attrs.hydraJobs or { }) ret.hydraJobs)
                else { ${system} = ret.${key}; };
            in
            attrs //
            {
              ${key} = (attrs.${key} or { })
              // (appendSystem key system ret);
            }
          ;
        in
        builtins.foldl' op attrs (builtins.attrNames ret);
    in
    builtins.foldl' op { } systems
  ;

  merger = flake:
    let
      filterAttrs = pred: set:
        listToAttrs (concatMap (name: let value = set.${name}; in if pred name value then [ ({ inherit name value; }) ] else [ ]) (attrNames set));

      systems = flake.systems or [ ];
    in
    mergeAny flake (
      eachSystem systems (system:
        let
          systemOutputs = flake.perSystem { inherit system; };
          otherArguments = flake;

          mkOutputs = attrs: output:
            attrs //
            mergeAny
              # prevent override of nested outputs in otherArguments
              (optionalAttrs (otherArguments ? ${output}.${system})
                { ${output} = otherArguments.${output}.${system}; })
              (optionalAttrs (systemOutputs ? ${output})
                { ${output} = systemOutputs.${output}; });
        in
        (foldl' mkOutputs { } (attrNames systemOutputs))
      )
    )
  ;

  lockFile = builtins.fromJSON lockFileStr;

  allNodes =
    builtins.mapAttrs
      (key: node:
        let

          sourceInfo =
            if key == lockFile.root
            then rootSrc
            else fetchTree (node.info or { } // removeAttrs node.locked [ "dir" ]);

          subdir = if key == lockFile.root then rootSubdir else node.locked.dir or "";

          flake = import (sourceInfo + (if subdir != "" then "/" else "") + subdir + "/flake.nix");

          inputs = builtins.mapAttrs
            (inputName: inputSpec: allNodes.${resolveInput inputSpec})
            (node.inputs or { });

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
            if path == [ ]
            then nodeName
            else
              getInputByPath
                # Since this could be a 'follows' input, call resolveInput.
                (resolveInput lockFile.nodes.${nodeName}.inputs.${builtins.head path})
                (builtins.tail path);

          outputs = merger (flake.outputs (inputs // { self = result; }));

          result = outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; };
        in
        if node.flake or true then
          assert builtins.isFunction flake.outputs;
          result
        else
          sourceInfo
      )
      lockFile.nodes;

in
allNodes.${lockFile.root}
