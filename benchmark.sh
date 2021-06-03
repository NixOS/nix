#!/bin/sh

set -euo pipefail
set -x

callNix () {
    nix \
        --experimental-features "nix-command flakes" \
        --store /tmp/nix \
        "$@"
}

callBuild () {
    callNix \
        eval --impure --file "$THINGTOBENCH" drvPath \
        "$@"
}
getCompletions () {
    NIX_GET_COMPLETIONS=6 callNix build "github:NixOS/nixpkgs?rev=ad0d20345219790533ebe06571f82ed6b034db31#firef" "$@"
}
runSearch () {
    callNix search "github:NixOS/nixpkgs?rev=ad0d20345219790533ebe06571f82ed6b034db31" firefox "$@"
}

noCache () {
    "$@" --option eval-cache false
}
coldCache () {
    if [[ -e ~/.cache/nix/eval-cache-v2 || -e ~/.cache/nix/eval-cache-v3 ]]; then
        echo "Error: The cache should be clean"
        exit 1
    fi
    "$@"
}

run_all () {

    mkdir -p "$out"

    NIX_SHOW_STATS=1 NIX_SHOW_STATS_PATH=$out/eval-stats.json bash $0 noCache callBuild

    hyperfine \
        --warmup 2 \
        --export-csv "$out/result.csv" \
        --export-json "$out/result.json" \
        --export-markdown "$out/result.md" \
        --style basic \
        --prepare '' "bash $0 noCache callBuild" \
        --prepare 'rm -rf ~/.cache/nix/' "bash $0 coldCache callBuild" \
        --prepare '' "bash $0 callBuild"
}

"$@"
