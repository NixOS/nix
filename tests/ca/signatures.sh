source common.sh

clearStore
clearCache

nix-store --generate-binary-cache-key cache1.example.org $TEST_ROOT/sk1 $TEST_ROOT/pk1
pk1=$(cat $TEST_ROOT/pk1)

export REMOTE_STORE_DIR="$TEST_ROOT/remote_store"
export REMOTE_STORE="file://$REMOTE_STORE_DIR"

ensureCorrectlyCopied () {
    attrPath="$1"
    nix build --store "$REMOTE_STORE" --file ./content-addressed.nix "$attrPath"
}

testOneCopy () {
    clearStore
    rm -rf "$REMOTE_STORE_DIR"

    attrPath="$1"
    nix copy -vvvv --to $REMOTE_STORE "$attrPath" --file ./content-addressed.nix \
        --secret-key-files "$TEST_ROOT/sk1" --show-trace

    ensureCorrectlyCopied "$attrPath"

    # Ensure that we can copy back what we put in the store
    clearStore
    nix copy --from $REMOTE_STORE \
        --file ./content-addressed.nix "$attrPath" \
        --trusted-public-keys $pk1
}

for attrPath in rootCA dependentCA transitivelyDependentCA dependentNonCA dependentFixedOutput; do
    testOneCopy "$attrPath"
done
