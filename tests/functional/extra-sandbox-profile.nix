{ destFile, seed }:

with import "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/config.nix";

mkDerivation {
  name = "simple";
  __sandboxProfile = ''
    # Allow writing any file in the filesystem
    (allow file*)
  '';
  inherit seed;
  buildCommand = ''
    (
      set -x
      touch ${destFile}
      touch $out
    )
  '';
}
