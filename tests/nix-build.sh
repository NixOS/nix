source common.sh

clearStore

(cd $TEST_ROOT && $nixbuild ../dependencies.nix)
test "$(cat $TEST_ROOT/result/foobar)" = FOOBAR

# The result should be retained by a GC.
echo A
target=$(readLink $TEST_ROOT/result)
echo B
echo target is $target
$nixstore --gc
test -e $target/foobar

# But now it should be gone.
rm $TEST_ROOT/result
$nixstore --gc
if test -e $target/foobar; then false; fi
