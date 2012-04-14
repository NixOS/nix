with import <nix/config.nix>;

let

  builder = builtins.toFile "nar.sh"
    ''
      export PATH=${nixBinDir}:${coreutils}

      echo "packing ‘$storePath’..."
      mkdir $out
      dst=$out/tmp.nar.bz2

      set -o pipefail
      nix-store --dump "$storePath" | ${bzip2} > $dst

      nix-hash --flat --type $hashAlgo --base32 $dst > $out/narbz2-hash

      mv $out/tmp.nar.bz2 $out/$(cat $out/narbz2-hash).nar.bz2
    '';

in

{ storePath, hashAlgo }:

derivation {
  name = "nar";
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" builder ];
  inherit storePath hashAlgo;
}
