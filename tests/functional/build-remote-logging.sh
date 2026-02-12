#!/usr/bin/env bash

# Test that structured log events are properly forwarded from remote builds.
# Specifically tests:
# - resSetPhase (type 104) forwarding
# - actBuild (type 105) start/stop events
# - resBuildLogLine (type 101) forwarding

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

echo "--- JSON output (build hook path) ---"

# resBuildLogLine (101) should always be forwarded
echo "Checking resBuildLogLine (101)..."
grepQuiet '"type":101' "$json_output"

# actBuild start (105) should appear
echo "Checking actBuild start (105)..."
grepQuiet '"type":105' "$json_output"

# resSetPhase (104) — this is the key test.
# The builder emits @nix {"action":"setPhase",...} messages.
# These should be forwarded as resSetPhase (type 104) results.
echo "Checking resSetPhase (104)..."
grepQuiet '"type":104' "$json_output"

# Check for specific phase names in the output
command grep '"buildPhase"' "$json_output" > /dev/null

# Forwarded activities from the remote daemon should have "forwarded":true.
# The remote Build activity is forwarded through the build hook, so at least
# one start event should carry the forwarded flag.
echo "Checking forwarded field on remote activities..."
grepQuiet '"forwarded":true' "$json_output"

echo "=== Test 1 PASSED ==="
