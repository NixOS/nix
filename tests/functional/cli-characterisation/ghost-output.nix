{
  type = "derivation";
  name = "ghost-output-1.0";
  outPath = builtins.toFile "out" "";
  out = {
    outPath = builtins.toFile "out" "";
  };
  outputs = [
    "out"
    "ghost"
  ];
  # no "ghost" attr -> silently skipped
}
