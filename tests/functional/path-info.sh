#!/usr/bin/env bash

source common.sh

echo foo > "$TEST_ROOT"/foo
foo=$(nix store add-file "$TEST_ROOT"/foo)
fooBase=$(basename "$foo")

echo bar > "$TEST_ROOT"/bar
bar=$(nix store add-file "$TEST_ROOT"/bar)
barBase=$(basename "$bar")

echo baz > "$TEST_ROOT"/baz
baz=$(nix store add-file "$TEST_ROOT"/baz)
bazBase=$(basename "$baz")
nix-store --delete "$baz"

diff --unified --color=always \
    <(nix path-info --json --json-format 2 "$foo" "$bar" "$baz" |
        jq --sort-keys '.info | map_values(.narHash)') \
    <(jq --sort-keys <<-EOF
        {
          "$fooBase": "sha256-QvtAMbUl/uvi+LCObmqOhvNOapHdA2raiI4xG5zI5pA=",
          "$barBase": "sha256-9fhYGu9fqxcQC2Kc81qh2RMo1QcLBUBo8U+pPn+jthQ=",
          "$bazBase": null
        }
EOF
    )

# Test that storeDir is returned in the JSON output in individual store objects
nix path-info --json --json-format 2 "$foo" | jq -e \
    --arg fooBase "$fooBase" \
    --arg storeDir "${NIX_STORE_DIR:-/nix/store}" \
    '.info[$fooBase].storeDir == $storeDir'

# And also at the top -evel
echo | nix path-info --json --json-format 2 --stdin | jq -e \
    --arg storeDir "${NIX_STORE_DIR:-/nix/store}" \
    '.storeDir == $storeDir'
