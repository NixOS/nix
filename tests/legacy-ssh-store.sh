source common.sh

# Check that store ping trusted doesn't yet work with ssh://
nix --store ssh://localhost?remote-store=$TEST_ROOT/other-store store ping --json | jq -e 'has("trusted") | not'
