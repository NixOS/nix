#!/bin/sh
set -x

echo Pushing "$OUTPUTS" to "$REMOTE_STORE"
printf "%s" "$OUTPUTS" | xargs nix copy --experimental-features 'ca-derivations ca-references nix-command' --to "$REMOTE_STORE" --no-require-sigs
