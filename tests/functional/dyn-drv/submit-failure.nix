with import ./config.nix;

let
  buildSubmitting =
    name: command:
    mkDerivation {
      inherit name;

      requiredSystemFeatures = [ "builder-rpc-v0" ];

      buildCommand = ''
        set -e
        set -u

        PATH=${builtins.getEnv "NIX_BIN_DIR"}:$PATH
        export NIX_CONFIG='extra-experimental-features = nix-command ca-derivations dynamic-derivations'

        ${command}
      '';

      __contentAddressed = true;
      outputHashMode = "nar";
      outputHashAlgo = "sha256";

    };
in
{
  duplicate = buildSubmitting "duplicate" ''
    mkdir a
    echo "miao" > a/gatto
    a="$(nix store add -n duplicate ./a)"
    nix store submit-output "$a" out

    mkdir b
    echo "miau" > b/katze
    b="$(nix store add -n duplicate ./b)"
    nix store submit-output "$b" out
  '';
  noSubmit = buildSubmitting "no-submit" ''
    mkdir a
    echo "nyaa" > a/neko
    a="$(nix store add -n no-submit ./a)"
    # Don't run the required `nix store submit-output`
  '';
}
