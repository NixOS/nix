source common.sh

testNormalization () {
    clearStore
    outPath=$(nix-build ./simple.nix --no-out-link)
    test "$(stat -c %Y $outPath)" -eq 1
}

testNormalization
