#!/usr/bin/env bash

# Every output of an in-process remote build comes back, not only the first.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

clearStore
cache="$TEST_ROOT/cache"
rm -rf "$cache"

# shellcheck disable=SC2016
outPaths=$(timeout 200 nix-build --store "file://$cache" --builders "ssh-ng://localhost - - 1 1" -j0 \
    --no-out-link -vvv -E '
  with import '"${config_nix}"';
  (mkDerivation {
    name = "inprocess-multi-output";
    outputs = [ "out" "dev" "doc" "man" ];
    buildCommand = "mkdir $out $dev $doc $man; echo out > $out/f; echo dev > $dev/f; echo doc > $doc/f; echo man > $man/f";
  }).all
' 2> "$TEST_ROOT/log")

[[ $(wc -l <<< "$outPaths") -eq 4 ]] || fail "expected four output paths, got: $outPaths"

# Only the in-process path traces "remote build done".
grepQuiet "on 'ssh-ng://localhost'" "$TEST_ROOT/log"
grepQuiet "remote build done" "$TEST_ROOT/log"
grepQuietInverse "starting build hook" "$TEST_ROOT/log"

for p in $outPaths; do
    nix path-info --store "file://$cache" "$p"
done
