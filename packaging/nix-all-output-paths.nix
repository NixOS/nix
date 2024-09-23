{ lib, formats, nix, packages, stdenv, ... }:

let
  randomPick = packages."x86_64-linux";
in
stdenv.mkDerivation {
  version = randomPick.version;
  pname = "nix-all-output-paths";
  passAsFile = [ "content" ];
  nativeBuildInputs = [ nix ];
  json = (formats.json {}).generate "all-output-paths.json" (lib.mapAttrs (name: pkg: "${lib.getBin pkg}") packages);
  nix = ''
    {
      ${
        lib.concatStringsSep "\n  " (
          lib.mapAttrsToList
            (name: pkg: "${lib.strings.escapeNixIdentifier name} = ${lib.strings.escapeNixString "${lib.getBin pkg}"};")
            packages
        )
      }
    }
  '';
  buildCommand = ''
    mkdir -p $out
    cp $json $out/all-output-paths.json
    echo "$nix" > $out/all-output-paths.nix

    mkdir -p $out/nix-support
    {
      echo "file all-output-paths $out/all-output-paths.json"
      echo "file all-output-paths-nix-deprecated $out/all-output-paths.nix"
    } >> $out/nix-support/hydra-build-products

    r=$(nix-instantiate --store dummy:// --expr --eval --json \
        'builtins.fromJSON (builtins.readFile '$out'/all-output-paths.json) == import '$out'/all-output-paths.nix')
    if ! [[ $r == "true" ]]; then
      echo "The generated Nix file does not match the JSON file"
      echo "Nix file: $out/all-output-paths.nix"
      cat -n $out/all-output-paths.nix
      echo "JSON file: $out/all-output-paths.json"
      cat -n $out/all-output-paths.json
      exit 1
    fi
  '';
  meta = {
    description = "A lookup file that gives a store path containing the Nix package manager for all supported system types";
  };
}
