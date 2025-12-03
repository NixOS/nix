#!/usr/bin/env bash

source common.sh

echo foo > "$TEST_ROOT"/foo
foo=$(nix store add-file "$TEST_ROOT"/foo)

echo bar > "$TEST_ROOT"/bar
bar=$(nix store add-file "$TEST_ROOT"/bar)

echo baz > "$TEST_ROOT"/baz
baz=$(nix store add-file "$TEST_ROOT"/baz)
nix-store --delete "$baz"

diff --unified --color=always \
    <(nix path-info --json "$foo" "$bar" "$baz" |
        jq --sort-keys 'map_values(.narHash)') \
    <(jq --sort-keys <<-EOF
        {
          "$foo": {
            "algorithm": "sha256",
            "format": "base64",
            "hash": "QvtAMbUl/uvi+LCObmqOhvNOapHdA2raiI4xG5zI5pA="
          },
          "$bar": {
            "algorithm": "sha256",
            "format": "base64",
            "hash": "9fhYGu9fqxcQC2Kc81qh2RMo1QcLBUBo8U+pPn+jthQ="
          },
          "$baz": null
        }
EOF
    )
