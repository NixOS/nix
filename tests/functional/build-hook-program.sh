#!/usr/bin/env bash

# A `build-hook` of `nix __build-remote` builds on the machine, not the worker.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS

cache="$TEST_ROOT/cache"

outPath=$(nix-build --store "file://$cache" --builders "ssh-ng://localhost - - 1 1" -j0 \
    --option build-hook "$(type -P nix) __build-remote" \
    dependencies.nix --no-out-link -vvv 2> "$TEST_ROOT/log")

nix path-info --store "file://$cache" "$outPath"

grepQuiet "starting build hook '" "$TEST_ROOT/log"
grepQuietInverse "remote build done" "$TEST_ROOT/log"
grepQuiet "on 'ssh-ng://localhost'" "$TEST_ROOT/log"
grepQuietInverse "build hook died unexpectedly" "$TEST_ROOT/log"
