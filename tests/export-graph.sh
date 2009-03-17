source common.sh

clearStore
clearProfiles

outPath=$($nixbuild ./export-graph.nix)

test $(nix-store -q --references ./result | wc -l) = 2 || fail "bad nr of references"
nix-store -q --references ./result | grep -q input-2 || fail "missing reference"
