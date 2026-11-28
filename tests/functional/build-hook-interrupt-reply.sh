#!/usr/bin/env bash

# Stopping the client while a goal waits for the hook's reply is not a crash.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

hook="$TEST_ROOT/hook.sh"
pidFile="$TEST_ROOT/hook-pid"
rm -f "$pidFile"

cat > "$hook" <<EOF
#!/bin/sh
echo \$\$ > "$pidFile"
exec sleep 1000
EOF
chmod +x "$hook"

echo "build-hook = $hook" >> "$test_nix_conf"
restartDaemon

# shellcheck disable=SC2016
nix-build --no-out-link -E '
  with import '"${config_nix}"';
  mkDerivation { name = "interrupt-reply"; buildCommand = "echo a > $out"; }
' 2> "$TEST_ROOT/log" &
client=$!

stop() {
    kill -KILL "$client" 2> /dev/null || true
    xargs kill -KILL < "$pidFile" 2> /dev/null || true
}
trap '[[ $? -eq 0 ]] || stop; killDaemon' EXIT

for _ in $(seq 60); do
    [[ -s $pidFile ]] && break
    sleep 0.5
done
[[ -s $pidFile ]] || { stop; fail "the hook did not start"; }
hookPid=$(< "$pidFile")

# Background jobs ignore SIGINT, and Nix handles SIGTERM the same way.
kill -TERM "$client"

for _ in $(seq 20); do
    kill -0 "$client" 2> /dev/null || break
    sleep 1
done
if kill -0 "$client" 2> /dev/null; then stop; fail "the client is still running"; fi
status=0
wait "$client" || status=$?

(( status != 0 && status < 128 )) || fail "the client ended with status $status: $(< "$TEST_ROOT/log")"
grepQuietInverse "Nix crashed" "$TEST_ROOT/log"

for _ in $(seq 20); do
    kill -0 "$hookPid" 2> /dev/null || break
    sleep 1
done
if kill -0 "$hookPid" 2> /dev/null; then stop; fail "the hook is still running"; fi
