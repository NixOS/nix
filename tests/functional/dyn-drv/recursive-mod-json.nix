with import ./config.nix;

let innerName = "foo"; in

mkDerivation rec {
  name = "${innerName}.drv";
  SHELL = shell;

  requiredSystemFeatures = [ "recursive-nix" ];

  drv = builtins.unsafeDiscardOutputDependency (import ./text-hashed-output.nix).hello.drvPath;

  buildCommand = ''
    export NIX_CONFIG='experimental-features = nix-command ca-derivations'

    PATH=${builtins.getEnv "EXTRA_PATH"}:$PATH

    # JSON of pre-existing drv
    nix derivation show $drv | jq .[] > drv0.json

    # Fix name
    jq < drv0.json '.name = "${innerName}"' > drv1.json

    # Extend `buildCommand`
    jq < drv1.json '.env.buildCommand += "echo \"I am alive!\" >> $out/hello\n"' > drv0.json

    # Used as our output
    cp $(nix derivation add < drv0.json) $out
  '';
  __contentAddressed = true;
  outputHashMode = "text";
  outputHashAlgo = "sha256";
}
