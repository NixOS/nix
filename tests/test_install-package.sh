source common.sh

# Note: this test expects to be run *after* nix-push.sh.

drvPath=$(nix-instantiate ./dependencies.nix)
outPath=$(nix-store -q $drvPath)

clearStore
clearProfiles

cat > $TEST_ROOT/foo.nixpkg <<EOF
NIXPKG1 file://$TEST_ROOT/cache/MANIFEST simple $system $drvPath $outPath
EOF

nix-install-package --non-interactive -p $profiles/test $TEST_ROOT/foo.nixpkg
test "$(nix-env -p $profiles/test -q '*' | wc -l)" -eq 1

clearProfiles

nix-install-package --non-interactive -p $profiles/test --url file://$TEST_ROOT/foo.nixpkg
test "$(nix-env -p $profiles/test -q '*' | wc -l)" -eq 1
