export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

drvPath=$(nix-instantiate "$NIX_TEST_ROOT/dependencies.nix")
outPath=$(nix-store -rvv "$drvPath")

# Set a GC root.
rm -f "$NIX_STATE_DIR"/gcroots/foo
ln -sf "$outPath" "$NIX_STATE_DIR"/gcroots/foo

[ "$(nix-store -q --roots "$outPath")" = "$NIX_STATE_DIR"/gcroots/foo ]

nix-store --gc --print-roots | grep "$outPath"
nix-store --gc --print-live | grep "$outPath"
# FIXME: nix-store --gc --print-dead | grep "$drvPath"
! nix-store --gc --print-dead | grep "$outPath"

nix-store --gc --print-dead

inUse=$(readLink "$outPath/input-2")
! nix-store --delete "$inUse"
test -e "$inUse"

! nix-store --delete "$outPath"
test -e "$outPath"

nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat "$outPath/foobar"
cat "$outPath/input-2/bar"

# Check that the derivation has been GC'd.
#FIXME: test -e $drvPath

rm "$NIX_STATE_DIR/gcroots/foo"

nix-collect-garbage

# Check that the output has been GC'd.
! test -e "$outPath/foobar"
