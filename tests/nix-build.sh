source common.sh

clearStore

outPath=$(nix-build dependencies.nix -o $TEST_ROOT/result)
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
if test -e $target/foobar; then false; fi

outPath2=$(nix-build $(nix-instantiate dependencies.nix) --no-out-link)
[[ $outPath = $outPath2 ]]

outPath2=$(nix-build $(nix-instantiate dependencies.nix)!out --no-out-link)
[[ $outPath = $outPath2 ]]
