let
  inherit (import ../config.nix) mkDerivation;
in
mkDerivation {
  name = "unusual-logging";
  buildCommand = ''
    {
      echo "@nix 1"
      echo "@nix {}"
      echo '@nix {"action": null}'
      echo '@nix {"action": 123}'
      echo '@nix ]['
    } >&$NIX_LOG_FD
    touch $out
  '';
}
