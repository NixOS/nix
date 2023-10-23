source common.sh

# Check that store info trusted doesn't yet work with ssh://
nix --store ssh://localhost?remote-store=$TEST_ROOT/other-store store info --json | jq -e 'has("trusted") | not'
