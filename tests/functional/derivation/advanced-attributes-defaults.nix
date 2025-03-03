derivation {
  name = "advanced-attributes-defaults";
  system = "my-system";
  builder = "/bin/bash";
  args = [
    "-c"
    "echo hello > $out"
  ];
}
