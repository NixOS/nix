#!/usr/bin/env bash

source common.sh

TODO_NixOS

# Using `--eval-store` with the daemon will eventually copy everything
# to the build store, invalidating most of the tests here
# shellcheck disable=SC1111
needLocalStore "“--eval-store” doesn't achieve much with the daemon"

eval_store=$TEST_ROOT/eval-store

clearStore
rm -rf "$eval_store"

nix build -f dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # Resolved CA derivations are written to store for building
    #
    # TODO when we something more systematic
    # (https://github.com/NixOS/nix/issues/5025) that distinguishes
    # between scratch storage for building and the final destination
    # store, we'll be able to make this unconditional again -- resolved
    # derivations should only appear in the scratch store.
    (! ls "$NIX_STORE_DIR"/*.drv)
fi
ls "$eval_store"/nix/store/*.drv

clearStore
rm -rf "$eval_store"

nix-instantiate dependencies.nix --eval-store "$eval_store"
(! ls "$NIX_STORE_DIR"/*.drv)
ls "$eval_store"/nix/store/*.drv

clearStore
rm -rf "$eval_store"

nix-build dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # See above
    (! ls "$NIX_STORE_DIR"/*.drv)
fi
ls "$eval_store"/nix/store/*.drv

clearStore
rm -rf "$eval_store"

# Confirm that import-from-derivation builds on the build store
[[ $(nix eval --eval-store "$eval_store?require-sigs=false" --impure --raw --file ./ifd.nix) = hi ]]
ls "$NIX_STORE_DIR"/*dependencies-top/foobar
(! ls "$eval_store"/nix/store/*dependencies-top/foobar)

# Can't write .drv by default
(! nix-instantiate dependencies.nix --eval-store "dummy://")
nix-instantiate dependencies.nix --eval-store "dummy://?read-only=false"
