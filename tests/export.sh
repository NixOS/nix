export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

outPath=$(nix-build $NIX_TEST_ROOT/dependencies.nix --no-out-link)

nix-store --export $outPath > $TEST_ROOT/exp

nix-store --export $(nix-store -qR $outPath) > $TEST_ROOT/exp_all


clearStore

if nix-store --import < $TEST_ROOT/exp; then
    echo "importing a non-closure should fail"
    exit 1
fi


clearStore

nix-store --import < $TEST_ROOT/exp_all

nix-store --export $(nix-store -qR $outPath) > $TEST_ROOT/exp_all2


clearStore

# Regression test: the derivers in exp_all2 are empty, which shouldn't
# cause a failure.
nix-store --import < $TEST_ROOT/exp_all2
