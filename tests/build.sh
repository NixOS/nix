source common.sh

clearStore

set -o pipefail

# Make sure that 'nix build' returns all outputs by default.
nix build -f multiple-outputs.nix --json a b --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.out | match(".*multiple-outputs-b"))))
'

# Test output selection using the '^' syntax.
nix build -f multiple-outputs.nix --json a^first --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first"]))
'

nix build -f multiple-outputs.nix --json a^second,first --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first", "second"]))
'

nix build -f multiple-outputs.nix --json 'a^*' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first", "second"]))
'

# Test that 'outputsToInstall' is respected by default.
nix build -f multiple-outputs.nix --json e --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a", "b"]))
'

# But not when it's overriden.
nix build -f multiple-outputs.nix --json e^a --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a"]))
'

nix build -f multiple-outputs.nix --json 'e^*' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a", "b", "c"]))
'

# Make sure that `--impure` works (regression test for https://github.com/NixOS/nix/issues/6488)
nix build --impure -f multiple-outputs.nix --json e --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a", "b"]))
'

testNormalization () {
    clearStore
    outPath=$(nix-build ./simple.nix --no-out-link)
    test "$(stat -c %Y $outPath)" -eq 1
}

testNormalization
