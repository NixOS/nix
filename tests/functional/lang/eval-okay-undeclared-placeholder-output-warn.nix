builtins.typeOf (builtins.derivationStrict {
  name = "test-undeclared-placeholder-warn";
  system = "x86_64-linux";
  builder = "/dontcare";
  foo = builtins.placeholder "bar";
})
