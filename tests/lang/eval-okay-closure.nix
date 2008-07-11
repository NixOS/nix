let

  closure = builtins.genericClosure {
    startSet = [{key = 80;}];
    operator = {key, foo ? false}:
      if builtins.lessThan key 0
      then []
      else [{key = builtins.sub key 9;} {key = builtins.sub key 13; foo = true;}];
  };

  sort = (import ./lib.nix).sortBy (a: b: builtins.lessThan a.key b.key);

in sort closure
