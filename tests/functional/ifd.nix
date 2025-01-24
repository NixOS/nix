with import "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/config.nix";
import (mkDerivation {
  name = "foo";
  bla = import ./dependencies.nix { };
  buildCommand = "
      echo \\\"hi\\\" > $out
    ";
})
