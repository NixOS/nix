#!/usr/bin/env bash

source common.sh

for ext in so dylib; do
    plugin="$PWD/plugins/libplugintest.$ext"
    [[ -f "$plugin" ]] && break
done

res=$(nix --option setting-set true --option plugin-files "$plugin" eval --expr builtins.anotherNull)

[ "$res"x = "nullx" ]
