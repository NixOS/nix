{system, narFile, outPath}: derivation {
  name = "unnar";
  builder = ./unnar.sh;
  system = system;
  narFile = narFile;
  outPath = outPath;
}
