export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

checkRef() {
    nix-store -q --references $TEST_ROOT/result | grep -q "$1" || fail "missing reference $1"
}

# Test the export of the runtime dependency graph.

outPath=$(nix-build "$NIX_TEST_ROOT/export-graph.nix" -A 'foo."bar.runtimeGraph"' -o $TEST_ROOT/result)

test $(nix-store -q --references $TEST_ROOT/result | wc -l) = 2 || fail "bad nr of references"

checkRef input-2
for i in $(cat $outPath); do checkRef $i; done

# Test the export of the build-time dependency graph.

nix-store --gc # should force rebuild of input-1

outPath=$(nix-build "$NIX_TEST_ROOT/export-graph.nix" -A 'foo."bar.buildGraph"' -o $TEST_ROOT/result)

checkRef input-1
checkRef input-1.drv
checkRef input-2
checkRef input-2.drv

for i in $(cat $outPath); do checkRef $i; done
