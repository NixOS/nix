{
  type = "derivation";
  name = "bad-output-specified-1.0";
  outPath = builtins.toFile "out" "";
  outputs = [ "out" ];
  outputSpecified = true;
  outputName = "nonexistent";
}
