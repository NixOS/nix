#!/usr/bin/env bash

source common.sh

cd "${TEST_ROOT}"

cleanup() {
    if [ -f "cache_http_pid" ]; then
        echo "==> shutting down HTTP server"
        kill "$(cat cache_http_pid)"
        rm cache_http_pid
    fi
}

# Trap the EXIT signal to ensure cleanup runs when the script finishes
trap cleanup EXIT

if [[ "$(uname)" == "Darwin" ]]; then
    echo "SKIP: Test not supported on MacOS due to sandboxing restrictions"
    exit 77
fi

# Find an available port by asking the OS for an ephemeral port
RANDOM_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("", 0)); print(s.getsockname()[1]); s.close()')
echo "==> using random port ${RANDOM_PORT}"

echo "==> ping to unopened port should fail"
if nix store ping --store "http://localhost:${RANDOM_PORT}" 2>/dev/null; then
    echo "ERROR: ping unexpectedly succeeded before server started"
    exit 1
fi

echo "==> creating file for binary cache"
rm -rf cache
mkdir -p cache
echo "dummy content for cache" > dummy-file.txt
storePath=$(nix store add-file ./dummy-file.txt)
nix copy --to "file://$PWD/cache" "$storePath"

echo "==> starting HTTP server for cache"
(
    cd cache || exit 1
    python3 -m http.server "${RANDOM_PORT}" --bind localhost >/dev/null 2>&1 &
    echo $! > ../cache_http_pid
)

# Wait for port to open (simple retry)
for i in {1..30}; do
    if curl -sSf -o /dev/null "http://localhost:${RANDOM_PORT}/" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

if ! curl -sSf -o /dev/null "http://localhost:${RANDOM_PORT}/" 2>/dev/null; then
    echo "ERROR: HTTP server did not start"
    kill "$(cat cache_http_pid)" 2>/dev/null || true
    exit 1
fi

echo "==> ping should succeed now"
if ! nix store ping --store "http://localhost:${RANDOM_PORT}"; then
    echo "ERROR: ping failed while server running"
    kill "$(cat cache_http_pid)" 2>/dev/null || true
    exit 1
fi

echo "==> stopping HTTP server"
kill "$(cat cache_http_pid)" 2>/dev/null || true
rm -f cache_http_pid


echo "==> ping should now fail after server shutdown"
# We expect this command to fail. By placing it in an 'if' condition,
# we prevent the 'set -e' trap from exiting the script immediately.
if nix store ping --store "http://localhost:${RANDOM_PORT}" &> /dev/null; then
    # This block runs if the command SUCCEEDS (exit code 0), which is the bug.
    echo "ERROR: ping unexpectedly SUCCEEDED after server shutdown (bug: info is cached)"
    exit 1
else
    # This block runs if the command FAILS (non-zero exit code), which is correct.
    exit_code=$?
    echo "==> test_ping_remote_cache_after_shutdown: OK (ping correctly failed with exit code $exit_code)"
fi
