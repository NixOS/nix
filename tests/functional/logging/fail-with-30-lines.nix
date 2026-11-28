with import ../config.nix;

mkDerivation {
  name = "fail-with-30-lines";
  buildCommand = ''
    for i in $(seq 1 30); do echo "error_line_$i" >&2; done
    exit 1
  '';
}
