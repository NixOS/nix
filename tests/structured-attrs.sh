source common.sh

clearStore

outPath=$(nix-build structured-attrs.nix --no-out-link)

[[ $(cat $outPath/foo) = bar ]]
