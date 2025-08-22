#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.36pre"
[[ $(uname) == Linux || $(uname) == Darwin ]] || skipTest "build termination is only supported on Linux and macOS"
TODO_NixOS # cannot enable ca-derivations in the system daemon
enableFeatures "ca-derivations"

if ! isTestOnNixOS; then
    # Compatibility tests start their daemon before enabling features.
    killDaemon
    startDaemon
fi

drvPath=$(nix-instantiate store-kill-build.nix)
lockPath="$drvPath.out.lock"

nix-store -r "$drvPath" > "$TEST_ROOT/build.out" 2> "$TEST_ROOT/build.err" &
buildPid=$!
cleanup() {
    if [[ -n "${buildPid-}" ]]; then
        kill "$buildPid" 2> /dev/null || true
    fi
    if [[ -n "${_NIX_TEST_DAEMON_PID-}" ]]; then
        killDaemon
    fi
}
trap cleanup EXIT

killOutput=
for _ in {1..100}; do
    killOutput=$(nix store kill-build "$drvPath" 2>&1 || true)
    grepQuiet "released the output locks" <<< "$killOutput" && break
    sleep 0.05
done

grepQuiet "released the output locks" <<< "$killOutput"

if wait "$buildPid"; then
    fail "build unexpectedly succeeded after its lock owner was terminated"
fi
buildPid=

[[ -e "$lockPath" ]]
