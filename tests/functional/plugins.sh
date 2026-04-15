#!/usr/bin/env bash

source common.sh

findPlugin() {
    local name="$1"

    for ext in so dylib; do
        local plugin="${_NIX_TEST_BUILD_DIR}/plugins/${name}.$ext"
        if [[ -f "$plugin" ]]; then
            printf '%s\n' "$plugin"
            return 0
        fi
    done

    return 1
}

plugin=$(findPlugin libplugintest)

res=$(nix --option setting-set true --option plugin-files "$plugin" eval --expr builtins.anotherNull)

[ "$res"x = "nullx" ]

if plugin_dlsym=$(findPlugin libplugintest_dlsym); then
    nix --option plugin-files "$plugin_dlsym" eval --expr 0 >/dev/null
fi
