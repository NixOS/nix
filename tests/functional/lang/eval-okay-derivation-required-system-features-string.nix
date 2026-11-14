# Intentionally exercises the deprecated space-separated-string form of `requiredSystemFeatures`
(builtins.derivationStrict {
  name = "eval-okay-derivation-required-system-features-string";
  system = "x86_64-linux";
  builder = "/dontcare";
  requiredSystemFeatures = "foo bar";
}).out
