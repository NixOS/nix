with import ./config.nix;

rec {

  public = builtins.toFile "public-file" "public content";
  secret = builtins.toSecretFile "secret-file" "secret content";
  fakeSecret = builtins.toFile "secret-file" "secret content";

  generateFiles = mkDerivation {
    name = "no-op";
    builder = builtins.toFile "builder"
      ''
        echo ${public} ${secret} ${fakeSecret} > $out/no-op
      '';
  };
}
