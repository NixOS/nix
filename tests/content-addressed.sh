source common.sh

clearStore
clearCache

out1=$(nix-build ./content-addressed.nix --arg seed 1)
out2=$(nix-build ./content-addressed.nix --arg seed 2)

test $(cat $out1) == $(cat $out2)
