# Intentionally exercises the deprecated splitting of `requiredSystemFeatures` list items that contain spaces
(builtins.derivationStrict {
  name = "eval-okay-derivation-required-system-features-list-spaces";
  system = "x86_64-linux";
  builder = "/dontcare";
  requiredSystemFeatures = [
    "foo bar"
    "baz"
  ];
}).out
