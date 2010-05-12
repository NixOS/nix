# this test shows how to use listToAttrs and that evaluation is still lazy (throw isn't called)
with import ./lib.nix;

let 
  asi = name: value : { inherit name value; };
  list = [ ( asi "a" "A" ) ( asi "b" "B" ) ];
  a = builtins.listToAttrs list;
  b = builtins.listToAttrs ( list ++ list );
  r = builtins.listToAttrs [ (asi "result" [ a b ]) ( asi "throw" (throw "this should not be thrown")) ];
in concat (map (x: x.a) r.result)
