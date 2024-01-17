source common.sh

clearStore

outPath=$(nix-build dependencies.nix --no-out-link)

nix-store --export $outPath > $TEST_ROOT/exp

nix-store --export $(nix-store -qR $outPath) > $TEST_ROOT/exp_all

if nix-store --export $outPath >/dev/full ; then
    echo "exporting to a bad file descriptor should fail"
    exit 1
fi


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
