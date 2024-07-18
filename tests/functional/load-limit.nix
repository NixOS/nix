with import ./config.nix;

mkDerivation {
  name = "load-limit";
  buildCommand = ''
    printf '%s' "''${NIX_LOAD_LIMIT-unset}" > "$out"
  '';
}
