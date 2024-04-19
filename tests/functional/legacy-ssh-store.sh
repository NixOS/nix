source common.sh

store_uri="ssh://localhost?remote-store=$TEST_ROOT/other-store"

# Check that store info trusted doesn't yet work with ssh://
nix --store "$store_uri" store info --json | jq -e 'has("trusted") | not'

# Suppress grumpiness about multiple nixes on PATH
(nix --store "$store_uri" doctor || true) 2>&1 | grep "doesn't have a notion of trusted user"
