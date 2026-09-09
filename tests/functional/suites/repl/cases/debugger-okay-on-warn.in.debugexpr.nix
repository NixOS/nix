with {
  msg = "hello";
  fails = throw (toString 1);
  succeeds = 1;
};
builtins.warn "this is a warning ${msg}" succeeds
