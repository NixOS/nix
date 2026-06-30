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

  dependency = buildSubmitting "dependency" ''
    mkdir dependency
    echo "this is a dependency" > dependency/foo
    out="$(nix store add --scan ./dependency)"
    nix store submit-output "$out" out
  '';
in
buildSubmitting "reference" ''
  mkdir mao
  echo "miao" > mao/foo
  echo "${dependency}" > mao/reference
  mao="$(nix store add --scan ./mao)"

  mkdir felis
  echo "miau" > felis/foo
  echo "$mao" > felis/reference
  felis="$(nix store add --scan -n reference ./felis)"

  nix store submit-output "$felis" out
''
