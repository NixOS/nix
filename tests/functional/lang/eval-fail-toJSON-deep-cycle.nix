let
  mk = depth: cyclePoint: if depth == 0 then cyclePoint else { next = mk (depth - 1) cyclePoint; };

  # 10 levels of nesting, then a cycle back to the top.
  loop = mk 10 loop;
in
builtins.toJSON loop
