with import ./config.nix;

mkDerivation {
  name = "simple-failing";
  builder = ./simple-failing.builder.sh;
  PATH = "";
  goodPath = path;
}
