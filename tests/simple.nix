with import ./config.nix;

mkDerivation {
  name = "simple";
  builder = ./simple.builder.sh;
  PATH = "";
  goodPath = path;
}
