#!/usr/bin/env bash

# An unreachable machine is passed over only for the build that could not
# reach it. The builds are chained, so each one tries it first.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
log="$TEST_ROOT/log"
rm -rf "$cache"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

unreachable="ssh-ng://localhost?remote-program=$TEST_ROOT/no-such-program - - 1 2"
reachable="ssh-ng://localhost - - 1 1"

# shellcheck disable=SC2016
outPath=$(timeout 200 nix-build --store "file://$cache" -j0 --no-out-link -vvv \
  --builders "$unreachable ; $reachable" -E '
  with import '"${config_nix}"';
  let
    first = mkDerivation { name = "inprocess-fallback-first"; buildCommand = "echo first > $out"; };
  in
  mkDerivation { name = "inprocess-fallback-second"; buildCommand = "echo ${first} > $out"; }
' 2> "$log")

nix path-info --store "file://$cache" "$outPath"
nix path-info --store "file://$cache" --recursive "$outPath" | grepQuiet inprocess-fallback-first

[[ $(grep -c "cannot build on 'ssh-ng://localhost.*no-such-program" "$log") -eq 2 ]]

[[ $(grep -c "remote build done" "$log") -eq 2 ]]
grepQuiet "inprocess-fallback-first.drv': remote build done" "$log"
grepQuiet "inprocess-fallback-second.drv': remote build done" "$log"

grepQuietInverse "starting build hook '" "$log"
grepQuietInverse "Nix crashed" "$log"
