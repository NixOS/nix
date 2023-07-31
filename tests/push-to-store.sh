#!/bin/sh

set -x
set -e

[ -n "$OUT_PATHS" ]
[ -n "$DRV_PATH" ]

echo Pushing "$OUT_PATHS" to "$REMOTE_STORE"
if [ -n "$BUILD_HOOK_ONLY_OUT_PATHS" ]; then
    printf "%s" "$OUT_PATHS" | xargs nix copy --to "$REMOTE_STORE" --no-require-sigs
else
    printf "%s" "$DRV_PATH"^'*' | xargs nix copy --to "$REMOTE_STORE" --no-require-sigs
fi
