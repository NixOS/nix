source common.sh

# First, test that -jN performs builds in parallel.
echo "testing nix-build -j..."

clearStore

rm -f $SHARED.cur $SHARED.max

outPath=$($nixbuild -j10000 parallel.nix)

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "abacade"; then exit 1; fi

if test "$(cat $SHARED.cur)" != 0; then fail "wrong current process count"; fi
if test "$(cat $SHARED.max)" != 3; then fail "not enough parallelism"; fi

# Second, test that parallel invocations of nix-build perform builds
# in parallel, and don't block waiting on locks held by the others.
echo "testing multiple nix-build -j1..."

clearStore

rm -f $SHARED.cur $SHARED.max

$nixbuild -j1 parallel.nix &
pid1=$!
echo "pid 1 is $pid1"

$nixbuild -j1 parallel.nix &
pid2=$!
echo "pid 2 is $pid2"

$nixbuild -j1 parallel.nix &
pid3=$!
echo "pid 3 is $pid3"

wait $pid1
wait $pid2
wait $pid3

if test "$(cat $SHARED.cur)" != 0; then fail "wrong current process count"; fi
if test "$(cat $SHARED.max)" != 3; then fail "not enough parallelism"; fi
