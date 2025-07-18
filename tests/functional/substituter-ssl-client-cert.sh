#!/usr/bin/env bash

# shellcheck source=common.sh
source common.sh

# These are not installed in vm_tests
[[ $(type -p curl) ]] || skipTest "curl is not installed"
[[ $(type -p openssl) ]] || skipTest "openssl is not installed"
[[ $(type -p python3) ]] || skipTest "python3 is not installed"

# Generate test certificates using EC keys for faster generation

# Generate CA with EC key
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/ca.key" 2>/dev/null
openssl req -new -x509 -days 1 -key "$TEST_ROOT/ca.key" -out "$TEST_ROOT/ca.crt" \
  -subj "/C=US/ST=Test/L=Test/O=TestCA/CN=Test CA" 2>/dev/null

# Generate server certificate with EC key
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/server.key" 2>/dev/null
openssl req -new -key "$TEST_ROOT/server.key" -out "$TEST_ROOT/server.csr" \
  -subj "/C=US/ST=Test/L=Test/O=TestServer/CN=localhost" 2>/dev/null
openssl x509 -req -days 1 -in "$TEST_ROOT/server.csr" -CA "$TEST_ROOT/ca.crt" -CAkey "$TEST_ROOT/ca.key" \
  -set_serial 01 -out "$TEST_ROOT/server.crt" 2>/dev/null

# Generate client certificate with EC key
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/client.key" 2>/dev/null
openssl req -new -key "$TEST_ROOT/client.key" -out "$TEST_ROOT/client.csr" \
  -subj "/C=US/ST=Test/L=Test/O=TestClient/CN=Nix Test Client" 2>/dev/null
openssl x509 -req -days 1 -in "$TEST_ROOT/client.csr" -CA "$TEST_ROOT/ca.crt" -CAkey "$TEST_ROOT/ca.key" \
  -set_serial 02 -out "$TEST_ROOT/client.crt" 2>/dev/null

# Find a free port
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("", 0)); print(s.getsockname()[1]); s.close()') \
    || skipTest "Cannot bind to a TCP port"

# Start the SSL cache server
python3 "${_NIX_TEST_SOURCE_DIR}/nix-binary-cache-ssl-server.py" \
  --port "$PORT" \
  --cert "$TEST_ROOT/server.crt" \
  --key "$TEST_ROOT/server.key" \
  --ca-cert "$TEST_ROOT/ca.crt" &
SERVER_PID=$!

# Function to stop server on exit
stopServer() {
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
}
trap stopServer EXIT

tries=0
while ! curl -v -s -k --cert "$TEST_ROOT/client.crt" --key "$TEST_ROOT/client.key" \
  "https://localhost:$PORT/nix-cache-info"; do
  if (( tries++ >= 50 )); then
    if kill -0 "$SERVER_PID" 2>/dev/null; then
      echo "Server started but did not respond in time" >&2
    else
      echo "Server failed to start" >&2
    fi
    exit 1
  fi
  sleep 0.1
done

# Test 1: Verify server rejects connections without client certificate
echo "Testing connection without client certificate (should fail)..." >&2
if curl -s -k "https://localhost:$PORT/nix-cache-info" 2>&1 | grep -q "certificate required"; then
  echo "FAIL: Server should have rejected connection" >&2
  exit 1
fi

# Test 2: Verify server accepts connections with client certificate
echo "Testing connection with client certificate..." >&2
RESPONSE=$(curl -v -s -k --cert "$TEST_ROOT/client.crt" --key "$TEST_ROOT/client.key" \
  "https://localhost:$PORT/nix-cache-info")

if ! echo "$RESPONSE" | grepQuiet "StoreDir: "; then
  echo "FAIL: Server should have accepted client certificate: $RESPONSE" >&2
  exit 1
fi

# Test 3: Test Nix with SSL client certificate parameters
# Set up substituter URL with SSL parameters
sslCache="https://localhost:$PORT?ssl-cert=$TEST_ROOT/client.crt&ssl-key=$TEST_ROOT/client.key"

# Configure Nix to trust our CA
export NIX_SSL_CERT_FILE="$TEST_ROOT/ca.crt"

# Test nix store info
nix store info --store "$sslCache" --json | jq -e '.url' | grepQuiet "https://localhost:$PORT"

# Test 4: Verify incorrect client certificate is rejected
# Generate a different client cert not signed by our CA (also using EC)
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/wrong.key" 2>/dev/null
openssl req -new -x509 -days 1 -key "$TEST_ROOT/wrong.key" -out "$TEST_ROOT/wrong.crt" \
  -subj "/C=US/ST=Test/L=Test/O=Wrong/CN=Wrong Client" 2>/dev/null

wrongCache="https://localhost:$PORT?ssl-cert=$TEST_ROOT/wrong.crt&ssl-key=$TEST_ROOT/wrong.key"

rm -rf "$TEST_HOME"

# This should fail
if nix store info --download-attempts 0 --store "$wrongCache"; then
  echo "FAIL: Should have rejected wrong certificate" >&2
  exit 1
fi
