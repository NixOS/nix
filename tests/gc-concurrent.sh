storeExpr=$($TOP/src/nix-instantiate/nix-instantiate gc-concurrent.nix)
outPath=$($TOP/src/nix-store/nix-store -q $storeExpr)

ls -l test-tmp/state/temproots


# Start a build in the background.
$TOP/src/nix-store/nix-store -rvv "$storeExpr" &
pid=$!

# Run the garbage collector while the build is running.
sleep 2
$NIX_BIN_DIR/nix-collect-garbage -vvvvv

# Wait for the build to finish.
echo waiting for pid $pid to finish...
wait $pid

# Check that the root and its dependencies haven't been deleted.
cat $outPath/foobar
cat $outPath/input-2/bar
