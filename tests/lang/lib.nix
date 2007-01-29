with builtins;

rec {

  fold = op: nul: list:
    if list == []
    then nul
    else op (head list) (fold op nul (tail list));

  concat =
    fold (x: y: x + y) "";

  flatten = x:
    if isList x
    then fold (x: y: (flatten x) ++ y) [] x
    else [x];

  sum = fold (x: y: add x y) 0;

  hasSuffix = ext: fileName:
    let lenFileName = stringLength fileName;
        lenExt = stringLength ext;
    in !(lessThan lenFileName lenExt) &&
       substring (sub lenFileName lenExt) lenFileName fileName == ext;

}
