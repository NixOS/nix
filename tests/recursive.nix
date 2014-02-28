with import ./config.nix;

mkDerivation {
  name = "recursive";
  builder = ./recursive.builder.sh;
  PATH = "${path}:${nixBinDir}";
  simple = builtins.toString ./simple.nix;
}
