storeExpr1=$($TOP/src/nix-instantiate/nix-instantiate gc-concurrent.nix)
outPath1=$($TOP/src/nix-store/nix-store -q $storeExpr1)

storeExpr2=$($TOP/src/nix-instantiate/nix-instantiate gc-concurrent2.nix)
outPath2=$($TOP/src/nix-store/nix-store -q $storeExpr2)

ls -l test-tmp/state/temproots

ln -s $storeExpr2 "$NIX_LOCALSTATE_DIR"/nix/gcroots/foo2

# Start build #1 in the background.  It starts immediately.
$TOP/src/nix-store/nix-store -rvv "$storeExpr1" &
pid1=$!

# Start build #2 in the background after 3 seconds.
(sleep 3 && $TOP/src/nix-store/nix-store -rvv "$storeExpr2") &
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
