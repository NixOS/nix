#!/bin/sh

set -x
set -e

[ -n "$OUT_PATHS" ]
[ -n "$DRV_PATH" ]
[ -n "$HOOK_DEST" ]

for o in $OUT_PATHS; do
    echo "$o" >> "$HOOK_DEST"
done
