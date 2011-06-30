with import ./config.nix;

mkDerivation {
  name = "timeout";
  builder = ./timeout.builder.sh;
  PATH = "";
  goodPath = path;
}
