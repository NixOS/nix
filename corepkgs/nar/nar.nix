{system, path, hashAlgo}: derivation {
  name = "nar";
  builder = ./nar.sh;
  inherit system path hashAlgo;
}
