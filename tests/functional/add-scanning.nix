with import ./config.nix;

let
  dependency = mkDerivation {
    name = "dependency";
    buildCommand = ''
      mkdir $out
      echo "this is a dependency" > $out/foo
    '';
  };
in
mkDerivation {
  name = "add-scanning";

  requiredSystemFeatures = [ "recursive-nix" ];

  buildCommand = ''
    set -euo pipefail

    PATH=${builtins.getEnv "EXTRA_PATH"}:$PATH
    export NIX_CONFIG='extra-experimental-features = nix-command'

    mkdir mao
    echo "miao" > mao/foo
    echo "${dependency}" > mao/reference
    mao="$(nix store add --scan ./mao)"

    mkdir felis
    echo "miau" > felis/foo
    echo "$mao" > felis/reference
    felis="$(nix store add --scan -n reference ./felis)"

    nix-store -qR "$felis" | grep "$mao"
    nix-store -qR "$felis" | grep "${dependency}"

    nix-store -qR "$felis" > "$out"
  '';
}
