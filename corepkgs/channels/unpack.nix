{system, inputs}:

derivation {
  name = "channels";
  builder = ./unpack.sh;
  inherit system inputs;
}