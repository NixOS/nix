locks: rootSrc:

let

  callFlake = sourceInfo: locks:
    let
      flake = import (sourceInfo + "/flake.nix");

      inputs = builtins.mapAttrs (n: v:
        if v.flake or true
        then callFlake (fetchTree v.locked) v.inputs
        else fetchTree v.locked) locks;

      outputs = flake.outputs (inputs // { self = result; });

      result = outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; };
    in
      assert flake.edition == 201909;

      result;

in callFlake rootSrc (builtins.fromJSON locks).inputs
