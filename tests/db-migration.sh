# Test that we can successfully migrate from an older db schema

# Only run this if we have an older Nix available
if [[ -z "$OUTER_NIX" ]]; then
    exit 0
fi

source common.sh

# Fill the db using the older Nix
PATH_WITH_NEW_NIX="$PATH"
export PATH="$OUTER_NIX/bin:$PATH"
clearStore
nix-build simple.nix --no-out-link
nix-store --generate-binary-cache-key cache1.example.org $TEST_ROOT/sk1 $TEST_ROOT/pk1
dependenciesOutPath=$(nix-build dependencies.nix --no-out-link --secret-key-files "$TEST_ROOT/sk1")
fixedOutPath=$(IMPURE_VAR1=foo IMPURE_VAR2=bar nix-build fixed.nix -A good.0 --no-out-link)

# Migrate to the new schema and ensure that everything's there
export PATH="$PATH_WITH_NEW_NIX"
info=$(nix path-info --json $dependenciesOutPath)
[[ $info =~ '"ultimate":true' ]]
[[ $info =~ 'cache1.example.org' ]]
nix verify -r "$fixedOutPath"
nix verify -r "$dependenciesOutPath" --sigs-needed 1 --trusted-public-keys $(cat $TEST_ROOT/pk1)
