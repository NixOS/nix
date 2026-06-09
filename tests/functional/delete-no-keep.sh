#!/usr/bin/env bash

source common.sh

TODO_NixOS

deleteNoKeep() {
    keep="$1"

    clearStore
    drvPath=$(nix-instantiate simple.nix)
    outPath=$(nix build -f simple.nix --no-link --print-out-paths)

    {
        echo "keep-outputs = false"
        echo "keep-derivations = false"
        echo "keep-$keep = true"
    } >> "$test_nix_conf"

    if [[ "$keep" = "outputs" ]]; then
        nix store delete "$outPath"
        [[ ! -e "$outPath" ]] || fail "$outPath should have been deleted"
    else
        nix store delete "$drvPath"
        [[ ! -e "$drvPath" ]] || fail "$drvPath should have been deleted"
    fi
}

if isDaemonNewer "2.35pre"; then
    deleteNoKeep outputs
    deleteNoKeep derivations
fi
