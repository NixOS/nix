# Intentionally exercises the deprecated space-separated-string form of `outputs`
(builtins.derivationStrict {
  name = "eval-okay-derivation-outputs-string";
  system = "x86_64-linux";
  builder = "/dontcare";
  outputs = "out dev";
}).out
