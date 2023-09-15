let 
  wat = arg : arg + builtins.break 10;
  foo = arg : arg2 : arg + arg2;
in
foo 12 (wat 11)
