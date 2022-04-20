source common.sh

clearStore

# Make sure that 'nix build' returns all outputs by default.
nix build -f multiple-outputs.nix --json a b --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys | length == 2) and
    (.outputs.first | match(".*multiple-outputs-a-first")) and
    (.outputs.second | match(".*multiple-outputs-a-second")))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs | keys | length == 1) and
    (.outputs.out | match(".*multiple-outputs-b")))
'

testNormalization () {
    clearStore
    outPath=$(nix-build ./simple.nix --no-out-link)
    test "$(stat -c %Y $outPath)" -eq 1
}

testNormalization
