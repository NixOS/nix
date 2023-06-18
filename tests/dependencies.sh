source common.sh

clearStore

drvPath=$(nix-instantiate dependencies.nix)

echo "derivation is $drvPath"

nix-store -q --tree "$drvPath" | grep '───.*builder-dependencies-input-1.sh'

# Test Graphviz graph generation.
nix-store -q --graph "$drvPath" > $TEST_ROOT/graph
if test -n "$dot"; then
    # Does it parse?
    $dot < $TEST_ROOT/graph
fi

outPath=$(nix-store -rvv "$drvPath") || fail "build failed"

# Test Graphviz graph generation.
nix-store -q --graph "$outPath" > $TEST_ROOT/graph
if test -n "$dot"; then
    # Does it parse?
    $dot < $TEST_ROOT/graph
fi

nix-store -q --tree "$outPath" | grep '───.*dependencies-input-2'

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "FOOBAR"; then exit 1; fi

deps=$(nix-store -quR "$drvPath")

echo "output closure contains $deps"

# The output path should be in the closure.
echo "$deps" | grepQuiet "$outPath"

# Input-1 is not retained.
if echo "$deps" | grepQuiet "dependencies-input-1"; then exit 1; fi

# Input-2 is retained.
input2OutPath=$(echo "$deps" | grep "dependencies-input-2")

# The referrers closure of input-2 should include outPath.
nix-store -q --referrers-closure "$input2OutPath" | grep "$outPath"

# Check that the derivers are set properly.
test $(nix-store -q --deriver "$outPath") = "$drvPath"
nix-store -q --deriver "$input2OutPath" | grepQuiet -- "-input-2.drv"

if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]] && ! isDaemonNewer "2.17pre0"; then
    echo "skipping valid deriver subtest"
else
    # instantiate a different drv with the same output
    drvPath2=$(nix-instantiate dependencies.nix --argstr hashInvalidator yay)
    nix-store --delete "$drvPath"
    # check that nix-store --deriver returns an existing drv if available
    test "$(nix-store -q --deriver "$outPath")" = "$drvPath2"

    nix-store --delete "$outPath"
    drvPath=$(nix-instantiate dependencies.nix)
    outPath=$(nix-store -rvv "$drvPath") || fail "build failed"

    # now the deriver of $outPath in the db is $drvPath but $drvPath2 is valid
    # and was registered sooner in the db
    # check that nix-store --deriver favors the stored deriver if valid
    test "$(nix-store -q --deriver "$outPath")" = "$drvPath"

    nix-store --delete "$drvPath"
    # $drvPath is not valid anymore, but there are others valid derivers
    # check that nix-store --deriver returns an a valid deriver if available
    test "$(nix-store -q --deriver "$outPath")" = "$drvPath2"

    nix-store --delete "$drvPath2"
    # check that nix-store --deriver returns the stored deriver is none is valid
    test "$(nix-store -q --deriver "$outPath")" = "$drvPath"
fi
