source common.sh

# Note: this test expects to be run *after* nix-push.sh.

drvPath=$($nixinstantiate ./dependencies.nix)
outPath=$($nixstore -q $drvPath)

clearStore
clearProfiles

cat > $TEST_ROOT/foo.nixpkg <<EOF
NIXPKG1 file://$TEST_ROOT/manifest simple $system $drvPath $outPath
EOF

$NIX_BIN_DIR/nix-install-package --non-interactive -p $profiles/test $TEST_ROOT/foo.nixpkg
test "$($nixenv -p $profiles/test -q '*' | wc -l)" -eq 1

clearProfiles

$NIX_BIN_DIR/nix-install-package --non-interactive -p $profiles/test --url file://$TEST_ROOT/foo.nixpkg
test "$($nixenv -p $profiles/test -q '*' | wc -l)" -eq 1
