{ destFile, seed }:

with import ./config.nix;

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
