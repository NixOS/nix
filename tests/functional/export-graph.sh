#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore
clearProfiles

checkRef() {
    nix-store -q --references "$TEST_ROOT"/result | grepQuiet "$1"'$' || fail "missing reference $1"
}

# Test the export of the runtime dependency graph.

outPath=$(nix-build ./export-graph.nix -A 'foo."bar.runtimeGraph"' -o "$TEST_ROOT"/result)

test "$(nix-store -q --references "$TEST_ROOT"/result | wc -l)" = 3 || fail "bad nr of references"

checkRef input-2
# shellcheck disable=SC2013
for i in $(cat "$outPath"); do checkRef "$i"; done

# Test the export of the build-time dependency graph.

nix-store --gc # should force rebuild of input-1

outPath=$(nix-build ./export-graph.nix -A 'foo."bar.buildGraph"' -o "$TEST_ROOT"/result)

checkRef input-1
checkRef input-1.drv
checkRef input-2
checkRef input-2.drv

# shellcheck disable=SC2013
for i in $(cat "$outPath"); do checkRef "$i"; done
