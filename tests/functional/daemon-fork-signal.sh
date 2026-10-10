#!/usr/bin/env bash

source common.sh

TODO_NixOS

# The daemon forks a worker for each client while a separate thread handles
# signals. If the fork happens while that thread holds the callback mutex, the
# worker inherits a permanently locked mutex and the client stops responding.

daemonPid=
signalPid=

cleanup() {
    set +e

    # Stop accepting connections before enumerating workers. A deadlocked worker
    # outlives both its client and the daemon, on any failure path.
    kill -STOP "$daemonPid" 2>/dev/null

    local workers=()
    if [[ $(uname) == Linux ]]; then
        read -r -a workers < "/proc/$daemonPid/task/$daemonPid/children" || true
    else
        local pid
        while read -r pid; do
            workers+=("$pid")
        done < <(ps -axo pid=,ppid= | awk -v parent="$daemonPid" '$2 == parent { print $1 }')
    fi

    kill -KILL "${workers[@]}" "$signalPid" "$daemonPid" 2>/dev/null
    wait "$signalPid" "$daemonPid" 2>/dev/null
}

killDaemon
startDaemon
daemonPid=$_NIX_TEST_DAEMON_PID
trap cleanup EXIT

# SIGWINCH exercises the callback registry mutex without requesting daemon
# shutdown, so the accept loop keeps creating workers during the signal flood.
# Flooding increases the chance of forking while the mutex is locked. The child
# inherits the locked mutex, but not the signal thread that could unlock it.
(
    set +x
    while kill -0 "$daemonPid" 2>/dev/null; do
        kill -WINCH "$daemonPid" 2>/dev/null || break
    done
) &
signalPid=$!

for ((attempt = 0; attempt < 100; attempt++)); do
    # Each connection makes the daemon fork a worker. A healthy worker answers
    # immediately; cap the wait so an affected worker cannot hang the test.
    clientStatus=0
    "$coreutils/timeout" --kill-after=1 10 nix store info > "$TEST_ROOT/client.log" 2>&1 || clientStatus=$?

    if [[ $clientStatus -ne 0 ]]; then
        cat "$TEST_ROOT/client.log" >&2
        fail "daemon client failed or timed out (status $clientStatus)"
    fi
done
