#!/usr/bin/env bash

# Test that a daemon can serve store paths purely over the socket,
# without requiring filesystem access to the store directory.
# This is important for VM setups where the host serves paths to
# the guest via socket, but the store directory is not shared.
#
# Can be called with daemon_backing_store_is_binary_cache=1 to test with a binary cache
# instead of a regular local store. See socket-only-daemon-binary-cache.sh.

source common.sh

needLocalStore "This test requires starting a separate daemon"

# Create state and cache locations
# Note: We use the same NIX_STORE_DIR (logical store path) as the test environment so paths are compatible
remote_cache_dir="$TEST_ROOT/remote-cache-$RANDOM"
remote_state_dir="$TEST_ROOT/remote-state-$RANDOM"
remote_real_store_dir="$TEST_ROOT/remote-real-store-$RANDOM"
remote_socket="$TEST_ROOT/remote-socket-initial"
moved_socket="$TEST_ROOT/moved-socket"

# Set up the remote store URI based on store type
if [[ "${daemon_backing_store_is_binary_cache:-0}" == "1" ]]; then
    echo "Using binary cache store"
    mkdir -p "$remote_cache_dir"
    remote_full_store_uri="file://$remote_cache_dir"
else
    echo "Using local store with different physical location"
    mkdir -p "$remote_state_dir"
    mkdir -p "$remote_real_store_dir"
    remote_full_store_uri="local?store=$NIX_STORE_DIR&real=$remote_real_store_dir&state=$remote_state_dir"
fi

# Create a test derivation file
cat > "$TEST_ROOT/test-derivation.nix" <<EOF
with import ${config_nix};
mkDerivation {
    name = "socket-test-path";
    buildCommand = "echo hello-from-remote > \$out";
}
EOF

# Build a test path in our local store
out=$(nix-build --no-out-link "$TEST_ROOT/test-derivation.nix")

echo "Built path: $out"

# Copy the path to the remote store
nix copy --to "$remote_full_store_uri" --no-check-sigs "$out"

# Verify it exists in the remote store
if [[ "${daemon_backing_store_is_binary_cache:-0}" == "1" ]]; then
    cache_hash=$(basename "$out" | cut -d- -f1)
    [[ -f "$remote_cache_dir/$cache_hash.narinfo" ]] || fail "Path not in binary cache"
else
    [[ -f "$out" ]] || fail "Path not in remote store"
fi

# Start a daemon for the remote store
rm -f "$remote_socket"
NIX_DAEMON_SOCKET_PATH="$remote_socket" \
    nix --extra-experimental-features 'nix-command' daemon --store "$remote_full_store_uri" &
remote_daemon_pid=$!

# Ensure daemon is cleaned up on exit
cleanup_daemon() {
    if [[ -n "${remote_daemon_pid:-}" ]]; then
        kill "$remote_daemon_pid" 2>/dev/null || true
        wait "$remote_daemon_pid" 2>/dev/null || true
    fi
}
trap cleanup_daemon EXIT

# Wait for socket to appear
for ((i = 0; i < 60; i++)); do
    if [[ -S "$remote_socket" ]]; then
        daemon_started=1
        break
    fi
    if ! kill -0 "$remote_daemon_pid"; then
        fail "Remote daemon died unexpectedly"
    fi
    sleep 0.1
done
[[ -n "${daemon_started:-}" ]] || fail "Remote daemon didn't start"

echo "Remote daemon started with PID $remote_daemon_pid"

# Move the socket to a different location to prevent any path-based
# assumptions from accidentally working (mildly paranoid, mildly effective;
# ideally we'd use a namespace, but that level of complexity is not actually
# needed)
mv "$remote_socket" "$moved_socket"

echo "Socket moved to: $moved_socket"

# Clear our local store so we need to substitute
clearStore

# Try to copy the path from the daemon via the moved socket
# NOTE: We do NOT pass the store location to the client - only the socket!
# The daemon must be able to serve paths knowing only what's in its own configuration.
nix copy --from "unix://$moved_socket" --no-require-sigs "$out"

# Verify the content
[[ -f "$out" ]] || fail "Output path doesn't exist"
[[ "$(cat "$out")" == "hello-from-remote" ]] || fail "Output content is wrong"

echo "Socket-only copy test PASSED"

# Clear the store again to test substituters mechanism
clearStore

# First verify that --max-jobs 0 without substituters fails (test our assumption)
if nix-build --max-jobs 0 --no-out-link "$TEST_ROOT/test-derivation.nix" 2>/dev/null; then
    fail "Building with --max-jobs 0 should have failed without substituters"
fi

echo "Confirmed: --max-jobs 0 without substituters fails as expected"

# Now test using the socket as a substituter with --max-jobs 0 (no building allowed)
# This ensures the substituter mechanism works, not just nix copy
nix-build --max-jobs 0 \
    --option substituters "unix://$moved_socket" \
    --option require-sigs false \
    --no-out-link \
    "$TEST_ROOT/test-derivation.nix"

echo "Socket-only substituter test PASSED"

# Test builders mechanism (only on Linux with daemon backed by local store)
# - Builders need sandboxing with namespace support to mount the correct store path
# - Binary cache stores can't build, only serve files
if [[ $(uname) == Linux && "${daemon_backing_store_is_binary_cache:-0}" == "0" ]]; then
    # Clear the store again to test builders mechanism
    clearStore

    # Test using the socket as a remote builder
    # This ensures the builders mechanism can also use socket-only connections
    nix-build \
        --option builders "unix://$moved_socket" \
        --option require-sigs false \
        --max-jobs 0 \
        --no-out-link \
        "$TEST_ROOT/test-derivation.nix"

    echo "Socket-only builder test PASSED"
fi
