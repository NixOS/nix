#!/usr/bin/env bash

# Test that structured log events are properly forwarded from remote builds.
# Specifically tests:
# - resSetPhase (result type 104) forwarding
# - actBuild (activity type 105) start events
# - resBuildLogLine (result type 101) forwarding
# - "machine" field on remote activities

source common.sh

requireSandboxSupport
requiresUnprivilegedUserNamespaces
[[ "${busybox-}" =~ busybox ]] || skipTest "no busybox"

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR

function join_by { local d=$1; shift; echo -n "$1"; shift; printf "%s" "${@/#/$d}"; }

EXTRA_SYSTEM_FEATURES=()
if [[ -n "${NIX_TESTS_CA_BY_DEFAULT-}" ]]; then
    EXTRA_SYSTEM_FEATURES=("ca-derivations")
fi

file=build-remote-logging.nix

# --- Test 1: Build hook path (ssh-ng:// in builders setting) ---

builders=(
    "ssh-ng://localhost?remote-store=$TEST_ROOT/machine1?system-features=$(join_by "%20" logging-test "${EXTRA_SYSTEM_FEATURES[@]}") - - 1 1 $(join_by "," logging-test "${EXTRA_SYSTEM_FEATURES[@]}")"
)

chmod -R +w "$TEST_ROOT/machine"* || true
rm -rf "$TEST_ROOT/machine"* || true

echo "=== Test 1: Build hook path (ssh-ng:// builder) ==="

json_output="$TEST_ROOT/json-output-hook.txt"
nix build -L -v --log-format internal-json \
    -f "$file" \
    -o "$TEST_ROOT/result-hook" \
    --max-jobs 0 \
    --arg busybox "$busybox" \
    --store "$TEST_ROOT/machine0" \
    --builders "$(join_by '; ' "${builders[@]}")" \
    2>"$json_output"

# Verify the build succeeded
outPath=$(readlink -f "$TEST_ROOT/result-hook")
grepQuiet 'done' "$TEST_ROOT/machine0/$outPath"

echo "--- Checking JSON output (build hook path) ---"

# resBuildLogLine (result type 101) should be forwarded.
# Use action-qualified pattern to avoid matching actFileTransfer (activity type 101).
echo "Checking resBuildLogLine (101)..."
grepQuiet '"action":"result".*"type":101' "$json_output"

# actBuild (activity type 105) start events should appear.
# Use action-qualified pattern to avoid matching resProgress (result type 105).
echo "Checking actBuild start (105)..."
grepQuiet '"action":"start".*"type":105' "$json_output"

# resSetPhase (result type 104) — the key test.
# The builder emits @nix {"action":"setPhase",...} messages which should
# be forwarded as resSetPhase (type 104) result events.
# Use action-qualified pattern to avoid matching actBuilds (activity type 104).
echo "Checking resSetPhase (104)..."
grepQuiet '"action":"result".*"type":104' "$json_output"

# Check for specific phase names in SetPhase results
command grep '"action":"result"' "$json_output" | grepQuiet '"buildPhase"'

# Forwarded activities from the remote daemon should carry a "machine"
# field identifying the remote store (not present on local activities).
echo "Checking machine field on remote start events..."
command grep '"action":"start"' "$json_output" | grepQuiet '"machine":"ssh-ng://'

# The machine field should also appear on result events forwarded
# from the remote daemon (via the build hook path).
echo "Checking machine field on remote result events..."
command grep '"action":"result"' "$json_output" | grepQuiet '"machine":"ssh-ng://'

# Remote activity IDs should be namespaced (bit 63 set) to prevent
# collisions with local IDs. In the JSON output, remote start events
# carrying a "machine" field should have large IDs (>= 2^63).
echo "Checking remote activity ID namespacing..."
python3 -c '
import json, sys
for line in open(sys.argv[1]):
    line = line.strip()
    if not line or line[0] != "@":
        continue
    # Strip the "@nix " prefix if present
    if line.startswith("@nix "):
        line = line[5:]
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        continue
    if obj.get("action") == "start" and "machine" in obj:
        act_id = obj["id"]
        assert act_id >= 2**63, f"Remote activity ID {act_id} does not have bit 63 set"
' "$json_output"

echo "=== Test 1 PASSED ==="
