with import ./config.nix;
mkDerivation {
  name = "mkdir-home-no-permission";
  builder = builtins.toFile "builder.sh"
    ''
      mkdir $HOME
    '';
}
