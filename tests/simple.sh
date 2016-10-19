source common.sh

drvPath=$(nix-instantiate simple.nix)

test "$(nix-store -q --binding system "$drvPath")" = "$system"

echo "derivation is $drvPath"

outPath=$(nix-store -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi

# Directed delete: $outPath is not reachable from a root, so it should
# be deleteable.
nix-store --delete $outPath
if test -e $outPath/hello; then false; fi

outPath="$(NIX_REMOTE=local?store=/foo\&real=$TEST_ROOT/real-store nix-instantiate --readonly-mode hash-check.nix)"
if test "$outPath" != "/foo/lfy1s6ca46rm5r6w4gg9hc0axiakjcnm-dependencies.drv"; then
    echo "hashDerivationModulo appears broken, got $outPath"
    exit 1
fi
