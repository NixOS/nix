{system, path}: derivation {
  name = "nar";
  builder = ./nar.sh;
  system = system;
  path = path;
}
