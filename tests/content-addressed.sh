source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

out1=$(nix-build ./content-addressed.nix --arg seed 1 --post-build-hook $PWD/push-to-store.sh)
out2=$(nix-build ./content-addressed.nix --arg seed 2)

nix-build ./content-addressed.nix --arg seed 3 |& (! grep -q "building transitively-dependent")

nix-store --verify-path $out1
nix-store --verify-path $out2

clearStore

nix-build ./content-addressed.nix --arg seed 1 \
  --substituters "file://$cacheDir" \
  --no-require-sigs \
  --max-jobs 0

# test $out1 == $out2
