with import ./config.nix;

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
