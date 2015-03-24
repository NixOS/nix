with import <nix/config.nix>;

let

  builder = builtins.toFile "nar.sh"
    ''
      export PATH=${nixBinDir}:${coreutils}

      if [ $compressionType = xz ]; then
        ext=.xz
        compressor="| ${xz} -7"
      elif [ $compressionType = bzip2 ]; then
        ext=.bz2
        compressor="| ${bzip2}"
      else
        ext=
        compressor=
      fi

      echo "packing ‘$storePath’..."
      mkdir $out
      dst=$out/tmp.nar$ext

      set -o pipefail
      eval "nix-store --dump \"$storePath\" $compressor > $dst"

      hash=$(nix-hash --flat --type $hashAlgo --base32 $dst)
      echo -n $hash > $out/nar-compressed-hash

      mv $dst $out/$hash.nar$ext
    '';

in

{ storePath, hashAlgo, compressionType }:

derivation {
  name = "nar";
  system = builtins.currentSystem;
  builder = shell;
  args = [ "-e" builder ];
  inherit storePath hashAlgo compressionType;

  # Remote machines may not have ${nixBinDir} or ${coreutils} in the same prefixes
  preferLocalBuild = true;

  inherit chrootDeps;
}
