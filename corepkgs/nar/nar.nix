{system, storePath, hashAlgo}: 

derivation {
  name = "nar";
  builder = ./nar.sh;
  inherit system storePath hashAlgo;
}
