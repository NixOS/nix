{
  type = "derivation";
  name = "bad-drvpath-1.0";
  outPath = builtins.toFile "out" "";
  drvPath = builtins.toFile "not-a-drv" "";
}
