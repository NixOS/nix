with builtins;

rec {

  fold = op: nul: list:
    if list == []
    then nul
    else op (head list) (fold op nul (tail list));

  concat =
    fold (x: y: x + y) "";

  and = fold (x: y: x && y) true;

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

  # Split a list at the given position.
  splitAt = pos: list:
    if pos == 0 then {first = []; second = list;} else
    if list == [] then {first = []; second = [];} else
    let res = splitAt (sub pos 1) (tail list);
    in {first = [(head list)] ++ res.first; second = res.second;};

  # Stable merge sort.
  sortBy = comp: list:
    if lessThan 1 (length list)
    then
      let
        split = splitAt (div (length list) 2) list;
        first = sortBy comp split.first;
        second = sortBy comp split.second;
      in mergeLists comp first second
    else list;

  mergeLists = comp: list1: list2:
    if list1 == [] then list2 else
    if list2 == [] then list1 else
    if comp (head list2) (head list1) then [(head list2)] ++ mergeLists comp list1 (tail list2) else
    [(head list1)] ++ mergeLists comp (tail list1) list2;

}
