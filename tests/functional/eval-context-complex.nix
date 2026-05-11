# Complex evaluation with nested function calls to demonstrate trace truncation
let
  # Helper functions at different lines to ensure position diversity
  helper1 = x: { value = x; };
  helper2 = x: (helper1 x).value + 1;
  helper3 = x: helper2 (helper2 x);
  helper4 = x: helper3 (helper3 x);
  helper5 = x: helper4 (helper4 x);
  
  # This will fail deep in the evaluation
  final = helper5 (throw "computation error deep in the stack");
in
final
