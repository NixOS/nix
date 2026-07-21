with import ./config.nix;

let
  baseAttrs = {
    name = "build-e.drv";

    requiredSystemFeatures = [ "builder-rpc-v0" ];

    buildCommand = ''
      set -e
      set -u

      if [[ ! -z "''${out+set}" ]]; then
        echo "out variable set in builder-rpc-v0 derivation"
        exit 1
      fi

      PATH=${builtins.getEnv "NIX_BIN_DIR"}:$PATH

      export NIX_CONFIG='extra-experimental-features = nix-command ca-derivations dynamic-derivations'

      declare -A deps=(
        [a]=""
        [b]="a"
        [c]="a"
        [d]="b c"
        [e]="b c d"
      )

      # Cannot just literally include this, or Nix will think it is the
      # *outer* derivation that's trying to refer to itself, and
      # substitute the string too soon.
      placeholder=$(nix eval --raw --expr 'builtins.placeholder "out"')

      declare -A drvs=()
      for word in a b c d e; do
        inputDrvs=""
        for dep in ''${deps[$word]}; do
          if [[ "$inputDrvs" != "" ]]; then
            inputDrvs+=","
          fi
          read -r -d "" line <<EOF || true
          "''${drvs[$dep]}": {
            "outputs": ["out"],
            "dynamicOutputs": {}
          }
      EOF
          inputDrvs+="$line"
        done
        read -r -d "" json <<EOF || true
        {
          "args": ["-c", "set -xeu; echo \"word env vav $word is \$$word\" >> \"\$out\""],
          "builder": "${shell}",
          "env": {
            "out": "$placeholder",
            "$word": "hello, from $word!",
            "PATH": ${builtins.toJSON path}
          },
          "inputs": {
            "drvs": {
              $inputDrvs
            },
            "srcs": []
          },
          "name": "build-$word",
          "outputs": {
            "out": {
              "method": "nar",
              "hashAlgo": "sha256"
            }
          },
          "system": "${system}",
          "version": 4
        }
      EOF
        drvPath=$(echo "$json" | nix derivation add)
        storeDir=$(dirname "$drvPath")
        drvs[$word]="$(basename "$drvPath")"
      done
      nix store submit-output "''${storeDir}/''${drvs[e]}" out
    '';

    __contentAddressed = true;
    outputHashMode = "text";
    outputHashAlgo = "sha256";
  };

  buildDynamic = attrs: builtins.outputOf (mkDerivation attrs).outPath "out";
in
{
  unstructured = buildDynamic baseAttrs;
  structured = buildDynamic (baseAttrs // { __structuredAttrs = true; });
}
