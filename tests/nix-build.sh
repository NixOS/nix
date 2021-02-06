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

outPath2=$(nix-store -r $(nix-instantiate --add-root $TEST_ROOT/indirect dependencies.nix)!out)
[[ $outPath = $outPath2 ]]

# The order of the paths on stdout must correspond to the -A options
# https://github.com/NixOS/nix/issues/4197

input0="$(nix-build nix-build-examples.nix -A input0 --no-out-link)"
input1="$(nix-build nix-build-examples.nix -A input1 --no-out-link)"
input2="$(nix-build nix-build-examples.nix -A input2 --no-out-link)"
body="$(nix-build nix-build-examples.nix -A body --no-out-link)"

outPathsA="$(echo $(nix-build nix-build-examples.nix -A input0 -A input1 -A input2 -A body --no-out-link))"
[[ "$outPathsA" = "$input0 $input1 $input2 $body" ]]

# test a different ordering to make sure it fails, not just in 23 out of 24 permutations
outPathsB="$(echo $(nix-build nix-build-examples.nix -A body -A input1 -A input2 -A input0 --no-out-link))"
[[ "$outPathsB" = "$body $input1 $input2 $input0" ]]
