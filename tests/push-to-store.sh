#!/bin/sh

echo Pushing "$@" to "$REMOTE_STORE"
printf "%s" "$OUT_PATHS" | xargs -d: nix copy --to "$REMOTE_STORE" --no-require-sigs
