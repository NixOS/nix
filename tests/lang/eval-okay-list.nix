let {

  fold = op: nul: list:
    if list == []
    then nul
    else op (builtins.head list) (fold op nul (builtins.tail list));

  concat =
    fold (x: y: x + y) "";

  body = concat ["foo" "bar" "bla" "test"];
    
}