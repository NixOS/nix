builtins.derivationStrict {
  name = "test-undeclared-placeholder";
  system = "x86_64-linux";
  builder = "/dontcare";
  foo = builtins.placeholder "bar";
}
