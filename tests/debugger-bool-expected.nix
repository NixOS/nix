#  should trigger exception in EvalState::evalBool

let 
  wat = arg : if ("wat") then arg else 10;
  foo = arg : arg2 : arg + arg2;
in
# call some functions to get some stack context
foo 12 (wat 11)