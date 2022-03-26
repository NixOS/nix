#!/bin/sh

set -x

echo Pushing "$OUT_PATHS" to "$REMOTE_STORE"
printf "%s" "$DRV_PATH" | xargs nix copy --to "$REMOTE_STORE" --no-require-sigs
