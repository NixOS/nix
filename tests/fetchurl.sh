source common.sh

clearStore

hash=$(nix-hash --flat --type sha256 ./fetchurl.nix)

outPath=$(nix-build ./fetchurl.nix --argstr filename $(pwd)/fetchurl.nix --argstr sha256 $hash)

cmp $outPath fetchurl.nix
