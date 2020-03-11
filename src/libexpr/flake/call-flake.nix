locks: rootSrc: rootSubdir:

let

  callFlake = sourceInfo: subdir: locks:
    let
      flake = import (sourceInfo + "/" + subdir + "/flake.nix");

      inputs = builtins.mapAttrs (n: v:
        if v.flake or true
        then callFlake (fetchTree (removeAttrs v.locked ["dir"])) (v.locked.dir or "") v.inputs
        else fetchTree v.locked) locks;

      outputs = flake.outputs (inputs // { self = result; });

      result = outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; };
    in
      assert flake.edition == 201909;

      result;

in callFlake rootSrc rootSubdir (builtins.fromJSON locks).inputs
