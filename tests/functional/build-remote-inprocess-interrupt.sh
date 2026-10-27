#!/usr/bin/env bash

# Stopping the client stops its remote build, and the client exits cleanly.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
started="$TEST_ROOT/started"
log="$TEST_ROOT/log"
rm -rf "$cache" "$started"
client=
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; [[ -n $client ]] && kill -KILL "$client" 2> /dev/null || true; killDaemon' EXIT

# shellcheck disable=SC2016
nix-build --store "file://$cache" --builders "ssh-ng://localhost - - 1 1" -j0 \
    --no-out-link -vvv -E '
  with import '"${config_nix}"';
  mkDerivation {
    name = "inprocess-interrupt";
    buildCommand = "echo $$ > '"$started"'; exec sleep 1000";
  }
' 2> "$log" &
client=$!

for _ in $(seq 60); do
    [[ -s $started ]] && break
    sleep 1
done
[[ -s $started ]] || fail "the remote build did not start"
builder=$(< "$started")
kill -0 "$builder" || fail "the remote builder is not running"

# Background jobs ignore SIGINT, and Nix handles SIGTERM the same way.
kill -TERM "$client"

for _ in $(seq 60); do
    kill -0 "$client" 2> /dev/null || break
    sleep 1
done
if kill -0 "$client" 2> /dev/null; then
    kill -KILL "$client" || true
    fail "the client is still running a minute after SIGTERM"
fi
status=0
wait "$client" || status=$?
[[ $status -ne 0 && $status -lt 128 ]] || fail "the client ended with status $status"
grepQuietInverse "Nix crashed" "$log"
grepQuiet "interrupted by the user" "$log"
# The builder check below cannot show that the coroutine finished, as the
# machine's daemon also stops the build once the client exits.
grepQuietInverse "still in flight when the worker went away" "$log"

for _ in $(seq 60); do
    kill -0 "$builder" 2> /dev/null || break
    sleep 1
done
if kill -0 "$builder" 2> /dev/null; then
    kill -KILL "$builder" || true
    fail "the remote builder is still running"
fi

grepQuiet "building '.*-inprocess-interrupt.drv' on 'ssh-ng://localhost'" "$log"
grepQuietInverse "starting build hook '" "$log"
