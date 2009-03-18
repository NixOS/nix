source common.sh

clearStore

outPath=$($nixbuild dependencies.nix)

$nixstore --export $outPath > $TEST_ROOT/exp

$nixstore --export $($nixstore -qR $outPath) > $TEST_ROOT/exp_all


clearStore

if $nixstore --import < $TEST_ROOT/exp; then
    echo "importing a non-closure should fail"
    exit 1
fi


clearStore

$nixstore --import < $TEST_ROOT/exp_all

$nixstore --export $($nixstore -qR $outPath) > $TEST_ROOT/exp_all2


clearStore

# Regression test: the derivers in exp_all2 are empty, which shouldn't
# cause a failure.
$nixstore --import < $TEST_ROOT/exp_all2
