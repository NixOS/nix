#!/bin/sh
set -x

echo Pushing "$OUTPUTS" to "$REMOTE_STORE"
printf "%s" "$OUTPUTS" | xargs -d: nix copy --to "$REMOTE_STORE" --no-require-sigs
