with import "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/config.nix";

mkDerivation {
  name = "simple-failing";
  builder = builtins.toFile "builder.sh"
    ''
      echo "This should fail"
      exit 1
    '';
  PATH = "";
  goodPath = path;
}
