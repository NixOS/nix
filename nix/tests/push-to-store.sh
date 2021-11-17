#!/bin/sh

set -x
set -e

[ -n "$OUT_PATHS" ]
[ -n "$DRV_PATH" ]

echo Pushing "$OUT_PATHS" to "$REMOTE_STORE"
printf "%s" "$DRV_PATH" | xargs nix copy --to "$REMOTE_STORE" --no-require-sigs
