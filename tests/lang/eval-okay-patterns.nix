let

  f = args@{x, y, z}: x + args.y + z;

  g = {x, y, z}@args: f args;

  h = {x ? "d", y ? x, z ? args.x}@args: x + y + z;

  i = args@args2: args.x + args2.y;

in
  f {x = "a"; y = "b"; z = "c";} +
  g {x = "x"; y = "y"; z = "z";} +
  h {x = "D";} +
  h {x = "D"; y = "E"; z = "F";} +
  i {x = "g"; y = "h";}
