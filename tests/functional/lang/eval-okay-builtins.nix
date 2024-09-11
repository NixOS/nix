assert builtins ? currentSystem;
assert !builtins ? __currentSystem;

let {

  x = if builtins ? dirOf then builtins.dirOf /foo/bar else "";

  y = if builtins ? fnord then builtins.fnord "foo" else "";

  body = x + y;
  
}
