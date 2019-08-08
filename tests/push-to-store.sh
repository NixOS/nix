#!/bin/sh

echo Pushing "$@" to "$REMOTE_STORE"
echo -n "$OUT_PATHS" | xargs -d: nix copy --to "$REMOTE_STORE" --no-require-sigs
