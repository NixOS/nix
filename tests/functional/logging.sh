#!/usr/bin/env bash

source common.sh

TODO_NixOS

path=$(nix-build dependencies.nix --no-out-link)

# Test nix-store -l.
[ "$(nix-store -l "$path")" = FOO ]

# Test compressed logs.
clearStore
rm -rf "$NIX_LOG_DIR"
(! nix-store -l "$path")
nix-build dependencies.nix --no-out-link --compress-build-log
[ "$(nix-store -l "$path")" = FOO ]

# test whether empty logs work fine with `nix log`.
builder=$TEST_ROOT/builder
echo -e "#!/bin/sh\nmkdir \$out" > "$builder"
outp="$(nix-build -E \
    'with import '"${config_nix}"'; mkDerivation { name = "fnord"; builder = '"$builder"'; }' \
    --out-link "$TEST_ROOT/result")"

test -d "$outp"

nix log "$outp"

if isDaemonNewer "2.26"; then
    # Build works despite ill-formed structured build log entries.
    expectStderr 0 nix build -f ./logging/unusual-logging.nix --no-link | grepQuiet -i 'warning: unable to handle a JSON message from the derivation builder:'
fi

# Test json-log-path.
if [[ "$NIX_REMOTE" != "daemon" ]]; then
    clearStore
    nix build -vv --file dependencies.nix --no-link --json-log-path "$TEST_ROOT/log.json" 2>&1 | grepQuiet 'building.*dependencies-top.drv'
    jq < "$TEST_ROOT/log.json"
    grep '{"action":"start","fields":\[".*-dependencies-top.drv","",1,1\],"id":.*,"level":3,"parent":0' "$TEST_ROOT/log.json" >&2
    (( $(grep -c '{"action":"msg","level":5,"msg":"executing builder .*"}' "$TEST_ROOT/log.json" ) == 5 ))
fi

# A failed build prints the log tail unless the client already showed the log.
# An extra JSON logger from json-log-path does not change that, and neither
# does a daemon, which learns from the client whether it shows the log.
testLogTail() {
    local expectedNixBuildStatus=100
    # work around the daemon not returning a 100 status correctly:
    # https://github.com/NixOS/nix/issues/4813
    if [[ "$NIX_REMOTE" == "daemon" ]]; then
        expectedNixBuildStatus=1
    fi

    clearStore
    # The tail leaves out the first log line.
    local out
    out=$(expectStderr 1 nix build -f ./logging/fail-with-30-lines.nix --no-link \
        --json-log-path "$TEST_ROOT/log.json")
    grepQuiet 'Last 25 log lines' <<< "$out"
    grepQuietInverse -w error_line_1 <<< "$out"
    out=$(expectStderr "$expectedNixBuildStatus" nix-build ./logging/fail-with-30-lines.nix --no-out-link --no-build-output)
    grepQuiet 'Last 25 log lines' <<< "$out"
    grepQuietInverse -w error_line_1 <<< "$out"

    # From 2.4 until 2.37, the daemon worker did not learn that the client
    # shows the log: https://github.com/NixOS/nix/issues/16128
    if isDaemonNewer "2.37.0pre"; then
        # Each check sees the first log line, which is not part of the tail, so
        # the full log was printed.
        out=$(expectStderr 1 nix build -f ./logging/fail-with-30-lines.nix --no-link --print-build-logs)
        grepQuiet -w error_line_1 <<< "$out"
        grepQuietInverse 'Last 25 log lines' <<< "$out"

        rm -f "$TEST_ROOT/log.json"
        out=$(expectStderr 1 nix build -f ./logging/fail-with-30-lines.nix --no-link --print-build-logs \
            --json-log-path "$TEST_ROOT/log.json")
        grepQuiet -w error_line_1 <<< "$out"
        grepQuietInverse 'Last 25 log lines' <<< "$out"
        grepQuiet -w error_line_1 "$TEST_ROOT/log.json"
        grepQuietInverse 'Last 25 log lines' "$TEST_ROOT/log.json"

        out=$(expectStderr "$expectedNixBuildStatus" nix-build ./logging/fail-with-30-lines.nix --no-out-link)
        grepQuiet -w error_line_1 <<< "$out"
        grepQuietInverse 'Last 25 log lines' <<< "$out"
    else
        # The older daemon prints the tail after the full log.
        out=$(expectStderr 1 nix build -f ./logging/fail-with-30-lines.nix --no-link --print-build-logs)
        grepQuiet -w error_line_1 <<< "$out"
        grepQuiet 'Last 25 log lines' <<< "$out"
    fi
}

testLogTail

# With NIX_DAEMON_PACKAGE, common.sh has started a daemon and testLogTail ran
# against it. Otherwise, repeat the checks against a daemon of this version,
# so that this test runs as part of the regular build.
if [[ "$NIX_REMOTE" != "daemon" ]]; then
    startDaemon
    testLogTail
    killDaemon

    # With json-log-path set for the daemon, its worker tees the logger that
    # talks to the client with a JSON logger.
    NIX_CONFIG="json-log-path = $TEST_ROOT/daemon-log.json" startDaemon
    testLogTail
    grepQuiet -w error_line_1 "$TEST_ROOT/daemon-log.json"
    killDaemon
fi
