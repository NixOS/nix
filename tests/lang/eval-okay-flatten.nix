let {

  fold = op: nul: list:
    if list == []
    then nul
    else op (builtins.head list) (fold op nul (builtins.tail list));

  concat =
    fold (x: y: x + y) "";
    
  flatten = x:
    if builtins.isList x
    then fold (x: y: (flatten x) ++ y) [] x
    else [x];

  l = ["1" "2" ["3" ["4"] ["5" "6"]] "7"];

  body = concat (flatten l);
}
