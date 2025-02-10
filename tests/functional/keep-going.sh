#!/usr/bin/env bash

source common.sh

clearStore

mkdir -p "$TEST_ROOT/sync"
# XXX: These invocations of nix-build should always return 100 according to the manpage, but often return 1
(! nix-build ./keep-going.nix -j2 --extra-sandbox-paths "$TEST_ROOT/sync" --no-out-link 2>&1 \
  | while read -r ln; do
    echo "nix build: $ln"
    if echo "$ln" | grepQuiet ...; then
      touch "$TEST_ROOT/sync/failing-drv-failed"
    fi
  done)
(! nix-build ./keep-going.nix -A good -j0 --no-out-link) || \
    fail "Hello shouldn't have been built because of earlier errors"

clearStore

(! nix-build ./keep-going.nix --keep-going -j2 --no-out-link)
nix-build ./keep-going.nix -A good -j0 --no-out-link || \
    fail "Hello should have been built despite the errors because of '--keep-going'"
