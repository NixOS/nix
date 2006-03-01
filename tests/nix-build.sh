source common.sh

$nixstore --gc

(cd $TEST_ROOT && $NIX_BIN_DIR/nix-build ../dependencies.nix)
test "$(cat $TEST_ROOT/result/foobar)" = FOOBAR

# The result should be retained by a GC.
target=$(ls -l $TEST_ROOT/result | sed 's/.*->\ //')
echo target is $target
$nixstore --gc
test -e $target/foobar

# But now it should be gone.
rm $TEST_ROOT/result
$nixstore --gc
if test -e $target/foobar; then false; fi
