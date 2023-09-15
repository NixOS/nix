let 
  wat = arg : if ("wat") then arg else 10;
  foo = arg : arg2 : arg + arg2;
in
foo 12 (wat 11)