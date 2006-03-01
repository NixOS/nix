source common.sh

drvPath1=$($nixinstantiate gc-concurrent.nix)
outPath1=$($nixstore -q $drvPath1)

drvPath2=$($nixinstantiate gc-concurrent2.nix)
outPath2=$($nixstore -q $drvPath2)

rm -f "$NIX_STATE_DIR"/gcroots/foo
ln -s $drvPath2 "$NIX_STATE_DIR"/gcroots/foo

# Start build #1 in the background.  It starts immediately.
$nixstore -rvv "$drvPath1" &
pid1=$!

# Start build #2 in the background after 3 seconds.
(sleep 3 && $nixstore -rvv "$drvPath2") &
pid2=$!

# Run the garbage collector while the build is running.  Note: the GC
# sleeps for *another* 2 seconds after acquiring the GC lock.  This
# checks whether build #1
sleep 2
NIX_DEBUG_GC_WAIT=1 $NIX_BIN_DIR/nix-collect-garbage -vvvvv

# Wait for build #1/#2 to finish.
echo waiting for pid $pid1 to finish...
wait $pid1
echo waiting for pid $pid2 to finish...
wait $pid2

# Check that the root of build #1 and its dependencies haven't been
# deleted.
cat $outPath1/foobar
cat $outPath1/input-2/bar

# Build #2 should have failed because its derivation got garbage collected.
cat $outPath2/foobar

rm "$NIX_STATE_DIR"/gcroots/foo
