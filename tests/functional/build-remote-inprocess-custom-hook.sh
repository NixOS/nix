#!/usr/bin/env bash

# With `build-hook` set, the worker does not pick a machine itself.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion
needLocalStore "the build hook is started by the process that builds"

hook="$TEST_ROOT/decline-hook.sh"
log="$TEST_ROOT/log"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

# `exec cat` drains the settings Nix writes, so they do not hit a closed pipe.
cat > "$hook" <<HOOK
#!/bin/sh
echo "# decline-permanently"
exec cat > /dev/null
HOOK
chmod +x "$hook"

machine="ssh-ng://localhost?remote-store=$TEST_ROOT/machine1"
rm -rf "$TEST_ROOT/machine1"

# shellcheck disable=SC2016
outPath=$(timeout 200 nix-build --no-out-link -vvv --max-jobs 1 \
  --option build-hook "$hook" --builders "$machine - - 1 1" -E '
  with import '"${config_nix}"';
  mkDerivation { name = "inprocess-custom-hook"; buildCommand = "echo local > $out"; }
' 2> "$log")

grep local "$outPath"

grepQuiet "starting build hook '$hook" "$log"
grepQuiet "hook reply is 'decline-permanently'" "$log"

grepQuietInverse "remote build done" "$log"
grepQuietInverse "on 'ssh-ng://localhost" "$log"
[[ ! -e "$TEST_ROOT/machine1" ]]
