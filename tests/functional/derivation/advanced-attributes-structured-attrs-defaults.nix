derivation {
  name = "advanced-attributes-structured-attrs-defaults";
  system = "my-system";
  builder = "/bin/bash";
  args = [
    "-c"
    "echo hello > $out"
  ];
  outputs = [
    "out"
    "dev"
  ];
  __structuredAttrs = true;
}
