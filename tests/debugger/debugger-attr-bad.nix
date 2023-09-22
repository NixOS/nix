let
  testattrs = { a = 1; b = "2"; };
  wat = arg : arg + testattrs.b;
  foo = arg : arg2 : arg + arg2;
in
foo 12 (wat 11)
