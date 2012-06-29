with import <nix/config.nix>;

let

  builder = builtins.toFile "nar.sh"
    ''
      export PATH=${nixBinDir}:${coreutils}

      echo "packing ‘$storePath’..."
      mkdir $out
      dst=$out/tmp.nar.xz

      set -o pipefail
      nix-store --dump "$storePath" | ${xz} -9 > $dst

      hash=$(nix-hash --flat --type $hashAlgo --base32 $dst)
      echo -n $hash > $out/nar-compressed-hash

      mv $dst $out/$hash.nar.xz
    '';

in

{ storePath, hashAlgo }:

derivation {
  name = "nar";
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" builder ];
  inherit storePath hashAlgo;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
