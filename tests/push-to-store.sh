#!/bin/sh

echo Pushing "$@" to "$REMOTE_STORE"
for OUT_PATH in $OUT_PATHS; do
  echo nix copy --to "$REMOTE_STORE" --no-require-sigs $OUT_PATH
  nix copy --to "$REMOTE_STORE" --no-require-sigs $OUT_PATH
done
