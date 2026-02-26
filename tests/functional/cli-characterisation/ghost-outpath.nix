{
  type = "derivation";
  name = "ghost-outpath-1.0";
  outPath = builtins.toFile "out" "";
  out = {
    outPath = builtins.toFile "out" "";
  };
  ghost = {
    # no outPath
  };
  outputs = [
    "out"
    "ghost"
  ];
}
