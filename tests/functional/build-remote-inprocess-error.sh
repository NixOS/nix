#!/usr/bin/env bash

# Failures on the in-process path are reported and do not crash.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
log="$TEST_ROOT/log"
rm -rf "$cache"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

# shellcheck disable=SC2016
expr='
  with import '"${config_nix}"';
  mkDerivation { name = "inprocess-error"; buildCommand = "echo e > $out"; }
'

status=0
timeout 200 nix-build -E "$expr" --store "file://$cache" -j0 --no-out-link -vvv \
  --builders "ssh-ng://localhost?remote-program=$TEST_ROOT/no-such-program - - 1 1" \
  2> "$log" || status=$?
[[ $status -eq 100 ]] || fail "expected exit status 100 with no machine and no local builds, got $status"
[[ $status -ne $((128 + 6)) && $status -ne $((128 + 11)) ]] || fail "the client died with status $status"
grepQuietInverse "Nix crashed" "$log"

grepQuiet "cannot build on 'ssh-ng://localhost.*no-such-program" "$log"
grepQuiet "Unable to build with a primary store that isn't a local store" "$log"
# Not "remote build done", which is traced even when no machine is reached.
grepQuietInverse "copying dependencies to" "$log"
grepQuietInverse "starting build hook '" "$log"

badBuilders="ssh-ng://localhost - - notanumber 1"
parseError="bad machine specification: failed to convert column #3 in a row"

status=0
timeout 200 nix-build -E "$expr" --store "file://$cache" -j0 --no-out-link -vvv \
  --builders "$badBuilders" 2> "$log" || status=$?
[[ $status -eq 1 ]] || fail "expected exit status 1 for an unparsable machine list, got $status"
grepQuiet "$parseError" "$log"
grepQuietInverse "Nix crashed" "$log"

# Still an error with local builds allowed.
# shellcheck disable=SC2016
expr2='
  with import '"${config_nix}"';
  [ (mkDerivation { name = "inprocess-error-local-a"; buildCommand = "echo a > $out"; })
    (mkDerivation { name = "inprocess-error-local-b"; buildCommand = "echo b > $out"; }) ]
'
status=0
timeout 200 nix-build --no-out-link -vvv -j2 --builders "$badBuilders" -E "$expr2" 2> "$log" || status=$?
[[ $status -eq 1 ]] || fail "expected exit status 1 for an unparsable machine list, got $status"
grepQuiet "$parseError" "$log"
for drvPath in $(nix-instantiate -E "$expr2"); do
    for outPath in $(nix-store -q --outputs "$drvPath"); do
        [[ ! -e $outPath ]] || fail "$outPath was built with an unparsable machine list"
    done
done
grepQuietInverse "remote build done" "$log"
grepQuietInverse "starting build hook '" "$log"
