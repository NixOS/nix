with import <nix/config.nix>;

let

  builder = builtins.toFile "nar.sh"
    ''
      export PATH=${nixBinDir}:${coreutils}

      if [ $compressionType = "xz" ]; then
        ext=xz
        compressor=${xz} -9
      else
        ext=bz2
        compressor=${bzip2}
      fi

      echo "packing ‘$storePath’..."
      mkdir $out
      dst=$out/tmp.nar.$ext

      set -o pipefail
      nix-store --dump "$storePath" | $compressor -9 > $dst

      hash=$(nix-hash --flat --type $hashAlgo --base32 $dst)
      echo -n $hash > $out/nar-compressed-hash

      mv $dst $out/$hash.nar.$ext
    '';

in

{ storePath, hashAlgo, compressionType }:

derivation {
  name = "nar";
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" builder ];
  inherit storePath hashAlgo compressionType;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
