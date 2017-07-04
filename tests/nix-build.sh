export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

nix-build $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result
test "$(cat $TEST_ROOT/result/foobar)" = FOOBAR

# The result should be retained by a GC.
echo A
target=$(readLink $TEST_ROOT/result)
echo B
echo target is $target
nix-store --gc
test -e $target/foobar

# But now it should be gone.
rm $TEST_ROOT/result
nix-store --gc
! test -e $target/foobar
