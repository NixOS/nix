let

  f = args@{x, y, z}: x + args.y + z;

  g = {x, y, z}@args: f args;

  h = {x ? "d", y ? x, z ? args.x}@args: x + y + z;

  j = {x, y, z, ...}: x + y + z;

in
  f {x = "a"; y = "b"; z = "c";} +
  g {x = "x"; y = "y"; z = "z";} +
  h {x = "D";} +
  h {x = "D"; y = "E"; z = "F";} +
  j {x = "i"; y = "j"; z = "k"; bla = "bla"; foo = "bar";}
