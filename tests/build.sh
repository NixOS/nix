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

# Test building from raw store path to drv not expression.

drv=$(nix eval -f multiple-outputs.nix --raw a.drvPath)
if nix build "$drv^not-an-output" --no-link --json; then
    fail "'not-an-output' should fail to build"
fi

if nix build "$drv^" --no-link --json; then
    fail "'empty outputs list' should fail to build"
fi

if nix build "$drv^*nope" --no-link --json; then
    fail "'* must be entire string' should fail to build"
fi

nix build "$drv^first" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.first | match(".*multiple-outputs-a-first")) and
      (has("second") | not)))
'

nix build "$drv^first,second" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
	  (.second | match(".*multiple-outputs-a-second"))))
'

nix build "$drv^*" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
	  (.second | match(".*multiple-outputs-a-second"))))
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

# https://github.com/NixOS/nix/issues/6572
issue_6572_independent_outputs() {
    nix build -f multiple-outputs.nix --json independent --no-link > $TEST_ROOT/independent.json

    # Make sure that 'nix build' can build a derivation that depends on both outputs of another derivation.
    p=$(nix build -f multiple-outputs.nix use-independent --no-link --print-out-paths)
    nix-store --delete "$p" # Clean up for next test

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    nix-store --delete "$(jq -r <$TEST_ROOT/independent.json .[0].outputs.first)"
    p=$(nix build -f multiple-outputs.nix use-independent --no-link --print-out-paths)
    cmp $p <<EOF
first
second
EOF
    nix-store --delete "$p" # Clean up for next test

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    nix-store --delete "$(jq -r <$TEST_ROOT/independent.json .[0].outputs.second)"
    p=$(nix build -f multiple-outputs.nix use-independent --no-link --print-out-paths)
    cmp $p <<EOF
first
second
EOF
    nix-store --delete "$p" # Clean up for next test
}
issue_6572_independent_outputs


# https://github.com/NixOS/nix/issues/6572
issue_6572_dependent_outputs() {

    nix build -f multiple-outputs.nix --json a --no-link > $TEST_ROOT/a.json

    # # Make sure that 'nix build' can build a derivation that depends on both outputs of another derivation.
    p=$(nix build -f multiple-outputs.nix use-a --no-link --print-out-paths)
    nix-store --delete "$p" # Clean up for next test

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    nix-store --delete "$(jq -r <$TEST_ROOT/a.json .[0].outputs.second)"
    p=$(nix build -f multiple-outputs.nix use-a --no-link --print-out-paths)
    cmp $p <<EOF
first
second
EOF
    nix-store --delete "$p" # Clean up for next test
}
if isDaemonNewer "2.12pre0"; then
    issue_6572_dependent_outputs
fi
