source common.sh

clearStore
clearCache

out1=$(nix-build ./content-addressed.nix --arg seed 1)
out2=$(nix-build ./content-addressed.nix --arg seed 2)

nix-build ./content-addressed.nix --arg seed 3 |& (! grep -q "building transitively-dependent")

nix-store --verify-path $out1
nix-store --verify-path $out2

# test $out1 == $out2
