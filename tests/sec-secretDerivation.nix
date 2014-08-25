with import ./config.nix;

let
  builder = builtins.toFile "builder.sh"
    ''
        echo 42 > $out/file
    '';
in

{
  public = mkDerivation {
    name = "public";
    inherit builder;
  };

  secret = mkDerivation {
    name = "secret";
    inherit builder;
    secret = true;
  };
}
