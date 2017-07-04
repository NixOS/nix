export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

drvPath=$(nix-instantiate $NIX_TEST_ROOT/simple.nix)

# $system is just the variable that nix was configured with...
system=$(nix-store -q --binding system "$drvPath")

echo "derivation is $drvPath"

outPath=$(nix-store -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi

# Directed delete: $outPath is not reachable from a root, so it should
# be deleteable.
nix-store --delete $outPath
! test -e $outPath/hello

outPath="$(NIX_REMOTE=local?store=/foo\&real=$TEST_ROOT/real-store nix-instantiate --readonly-mode $NIX_TEST_ROOT/hash-check.nix)"
if test "$outPath" != "/foo/lfy1s6ca46rm5r6w4gg9hc0axiakjcnm-dependencies.drv"; then
    echo "hashDerivationModulo appears broken, got $outPath"
    exit 1
fi
