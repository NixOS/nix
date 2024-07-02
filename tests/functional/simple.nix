with import ./config.nix;

mkDerivation {
  name = "simple";
  builder = ./simple.builder.sh;
  PATH = "";
  goodPath = path;
  meta.position = "${__curPos.file}:${toString __curPos.line}";
}
