source common.sh

###################################################
# Check that --dry-run isn't confused with read-only mode
# https://github.com/NixOS/nix/issues/1795

clearStore
clearCache

# Ensure this builds successfully first
nix build -f dependencies.nix

clearStore
clearCache

# Try --dry-run using old command first
nix-build dependencies.nix --dry-run 2>&1 | grep "will be built"
# Now new command:
nix build -f dependencies.nix --dry-run 2>&1 | grep "will be built"

clearStore
clearCache

# Try --dry-run using new command first
nix build -f dependencies.nix --dry-run 2>&1 | grep "will be built"
# Now old command:
nix-build dependencies.nix --dry-run 2>&1 | grep "will be built"


###################################################
# Check --dry-run doesn't create links with --dry-run
# https://github.com/NixOS/nix/issues/1849
clearStore
clearCache

RESULT=$TEST_ROOT/result-link
rm -f $RESULT

nix-build dependencies.nix -o $RESULT --dry-run

[[ ! -h $RESULT ]] || fail "nix-build --dry-run created output link"

nix build -f dependencies.nix -o $RESULT --dry-run

[[ ! -h $RESULT ]] || fail "nix build --dry-run created output link"

nix build -f dependencies.nix -o $RESULT

[[ -h $RESULT ]]
