{
  type = "derivation";
  name = "bad-output-specified-type-1.0";
  outPath = builtins.toFile "out" "";
  outputs = [ "out" ];
  out = {
    outPath = builtins.toFile "out" "";
  };
  outputSpecified = "yes";
}
