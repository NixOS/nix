with import ./config.nix;

builtins.outputOf
  (mkDerivation {
    name = "make-derivations.drv";

    requiredSystemFeatures = [ "recursive-nix" ];

    buildCommand = ''
      set -e
      set -u

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
      cp "''${storeDir}/''${drvs[e]}" $out
    '';

    __contentAddressed = true;
    outputHashMode = "text";
    outputHashAlgo = "sha256";
  }).outPath
  "out"
