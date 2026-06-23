#!/usr/bin/env bash

# See https://github.com/NixOS/nix/issues/15916

source common.sh

TODO_NixOS # Requires substituting from a local binary cache, enable when we sign paths in NixOS tests
needLocalStore "'--no-require-sigs' can’t be used with the daemon"

BINARY_CACHE=file://$cacheDir

readarray -t outPaths < <(nix build -f multiple-outputs.nix 'independent^*' --no-link --print-out-paths)
[[ ${#outPaths[@]} -eq 2 ]]
nix copy --to "$BINARY_CACHE" "${outPaths[@]}"
for p in "${outPaths[@]}"; do
    [[ $p == *-second ]] && secondOut=$p
done

# Corrupt the second output, so that the substitution partially succeeds.
secondNarInfoFile="$cacheDir/$(basename "$secondOut" | cut -c1-32).narinfo"
sed -i 's|^NarHash:.*|NarHash: sha256:0000000000000000000000000000000000000000000000000000|' "$secondNarInfoFile"

clearStore
clearCacheCache

# Note that using "^*" matters here. We want all wanted outputs for the same goal.
expect 1 nix build -j 0 -f multiple-outputs.nix "independent^*" --no-link \
    --substituters "$BINARY_CACHE" --no-require-sigs --substitute 2>&1 | grepQuiet "hash mismatch"
