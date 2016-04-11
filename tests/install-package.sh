source common.sh

drvPath=$(nix-instantiate ./dependencies.nix)
outPath=$(nix-store -r $drvPath)
nix-push --dest $cacheDir $outPath

clearStore
clearProfiles

cat > $TEST_ROOT/foo.nixpkg <<EOF
NIXPKG1 - simple $system $drvPath $outPath file://$cacheDir
EOF

nix-install-package --non-interactive -p $profiles/test $TEST_ROOT/foo.nixpkg
test "$(nix-env -p $profiles/test -q '*' | wc -l)" -eq 1

clearProfiles

nix-install-package --non-interactive -p $profiles/test --url file://$TEST_ROOT/foo.nixpkg
test "$(nix-env -p $profiles/test -q '*' | wc -l)" -eq 1
