#!/bin/sh

set -x

echo Pushing "$@" to "$REMOTE_STORE"
printf "%s" "$DRV_OUTPUTS" | xargs nix copy --to "$REMOTE_STORE" --no-require-sigs
