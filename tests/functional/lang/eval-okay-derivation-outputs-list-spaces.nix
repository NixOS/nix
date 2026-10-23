# Intentionally exercises the deprecated splitting of `outputs` list items that contain spaces
(builtins.derivationStrict {
  name = "eval-okay-derivation-outputs-list-spaces";
  system = "x86_64-linux";
  builder = "/dontcare";
  outputs = [
    "out dev"
    "bin"
  ];
}).out
