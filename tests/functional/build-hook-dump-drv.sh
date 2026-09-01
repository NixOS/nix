#!/bin/sh

set -x
set -e

[ -n "$OUT_PATHS" ]
[ -n "$DRV_PATH" ]
[ -n "$RESOLVED_DRV_PATH" ]
[ -n "$DRV_ATERM_FD" ]
[ -n "$BUILD_INFO_JSON_FD" ]
[ -n "$HOOK_DEST" ]

case "$DRV_PATH" in
    *-"$DRV_NAME".drv) ;;
    *) exit 1 ;;
esac

printf '%s' "$DRV_NAME" > "$HOOK_DEST/name"
printf '%s' "$RESOLVED_DRV_PATH" > "$HOOK_DEST/resolved-drv-path"
eval "cat <&$DRV_ATERM_FD" > "$HOOK_DEST/drv.aterm"
eval "cat <&$BUILD_INFO_JSON_FD" > "$HOOK_DEST/build-info.json"
