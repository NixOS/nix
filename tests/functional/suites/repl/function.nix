{
  x ? 1,
}:
import ((builtins.getEnv "_NIX_TEST_SOURCE_DIR") + "/simple.nix")
