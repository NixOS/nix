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
            "format": "base16",
            "hash": "42fb4031b525feebe2f8b08e6e6a8e86f34e6a91dd036ada888e311b9cc8e690"
          },
          "$bar": {
            "algorithm": "sha256",
            "format": "base16",
            "hash": "f5f8581aef5fab17100b629cf35aa1d91328d5070b054068f14fa93e7fa3b614"
          },
          "$baz": null
        }
EOF
    )
