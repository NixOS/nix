source common.sh

outPath=$(nix-build --no-out-link readfile-context.nix)

# Set a GC root.
rm -f "$NIX_STATE_DIR"/gcroots/foo
ln -sf $outPath "$NIX_STATE_DIR"/gcroots/foo

# Check that file exists.
[ "$(cat $(cat $outPath))" = "FOO" ]

nix-collect-garbage

# Check that file still exists.
[ "$(cat $(cat $outPath))" = "FOO" ]
