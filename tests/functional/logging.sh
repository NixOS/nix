#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

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
builder="$(realpath "$(mktemp)")"
echo -e "#!/bin/sh\nmkdir \$out" > "$builder"
outp="$(nix-build -E \
    'with import '"${config_nix}"'; mkDerivation { name = "fnord"; builder = '"$builder"'; }' \
    --out-link "$(mktemp -d)/result")"

test -d "$outp"

nix log "$outp"

if isDaemonNewer "2.26"; then
    # Build works despite ill-formed structured build log entries.
    expectStderr 0 nix build -f ./logging/unusual-logging.nix --no-link | grepQuiet 'warning: Unable to handle a JSON message from the derivation builder:'
fi

# Test json-log-path.
if [[ "$NIX_REMOTE" != "daemon" ]]; then
    clearStore
    nix build -vv --file dependencies.nix --no-link --json-log-path "$TEST_ROOT/log.json" 2>&1 | grepQuiet 'building.*dependencies-top.drv'
    jq < "$TEST_ROOT/log.json"
    grep '{"action":"start","fields":\[".*-dependencies-top.drv","",1,1\],"id":.*,"level":3,"parent":0' "$TEST_ROOT/log.json" >&2
    (( $(grep -c '{"action":"msg","level":5,"msg":"executing builder .*"}' "$TEST_ROOT/log.json" ) == 5 ))

    # Test that upfront build totals are signaled via setExpected before builds start.
    # resSetExpected = 106, actBuilds = 104
    # The setExpected result for builds should appear before the first build activity starts.
    first_build_line=$(grep -n '"action":"start".*"type":105' "$TEST_ROOT/log.json" | head -1 | cut -d: -f1)
    setexpected_builds_line=$(grep -n '"action":"result".*"fields":\[104,.*"type":106' "$TEST_ROOT/log.json" | head -1 | cut -d: -f1)
    [[ -n "$setexpected_builds_line" ]] || { echo "setExpected for builds not found"; exit 1; }
    [[ -n "$first_build_line" ]] || { echo "first build activity not found"; exit 1; }
    (( setexpected_builds_line < first_build_line )) || { echo "setExpected for builds ($setexpected_builds_line) should appear before first build ($first_build_line)"; exit 1; }
    # Verify the expected build count is 5 (dependencies-top + input-0 + input-1 + input-2 + fod-input)
    jq -e 'select(.action == "result" and .type == 106 and .fields[0] == 104 and .fields[1] == 5)' "$TEST_ROOT/log.json" > /dev/null || { echo "Expected 5 builds in setExpected"; exit 1; }
fi
