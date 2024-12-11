with import "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/config.nix";

mkDerivation {
  name = "simple";
  builder = ./simple.builder.sh;
  PATH = "";
  goodPath = path;
  meta.position = "${__curPos.file}:${toString __curPos.line}";
}
