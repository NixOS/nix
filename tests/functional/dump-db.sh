#!/usr/bin/env bash

source common.sh

TODO_NixOS

needLocalStore "--dump-db requires a local store"

clearStore

nix-build dependencies.nix -o "$TEST_ROOT"/result
deps="$(nix-store -qR "$TEST_ROOT"/result)"

if [[ "${_NIX_TEST_DERIVATIONS_IN_DB:-}" = 1 ]]; then
    # DB-stored derivations cannot round-trip through dump-db/load-db yet,
    # so remove them before dumping.
    nix-store --delete $(nix path-info --all | grep '\.drv$') || true
fi

nix-store --dump-db > "$TEST_ROOT"/dump

rm -rf "$NIX_STATE_DIR"/db

nix-store --load-db < "$TEST_ROOT"/dump

deps2="$(nix-store -qR "$TEST_ROOT"/result)"

[ "$deps" = "$deps2" ];

nix-store --dump-db > "$TEST_ROOT"/dump2
cmp "$TEST_ROOT"/dump "$TEST_ROOT"/dump2
