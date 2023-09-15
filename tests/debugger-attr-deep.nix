let 
  testattrs = { a = 1; b = wat 5; };
  wat = arg : arg + testattrs.b;
  foo = arg : arg2 : arg + arg2;
  
in
testattrs