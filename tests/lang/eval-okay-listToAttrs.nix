# this test shows how to use listToAttrs and that evaluation is still lazy (throw isn't called)
let 
  asi = attr: value : { inherit attr value; };
  list = [ ( asi "a" "A" ) ( asi "b" "B" ) ];
  a = builtins.listToAttrs list;
  b = builtins.listToAttrs ( list ++ list );
  r = builtins.listToAttrs [ (asi "result" [ a b ]) ( asi "throw" (throw "this should not be thrown")) ];
in r.result
