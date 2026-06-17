#!/usr/bin/env bash

source common.sh

# Long AF_UNIX socket paths must not break the daemon's bind nor the client's
# connect, even when the absolute path exceeds sockaddr_un::sun_path.

# This test requires control of the daemon's socket path.
isTestOnNixOS && skipTest "this test manages its own daemon socket path"

requireDaemonNewerThan "2.0"

needLocalStore "the daemon socket path is part of this test"

# Build a path well past Linux's 108-byte sun_path.
deep="$TEST_ROOT/$(printf 'padding-segment-/%.0s' {1..10})z"
sock="$deep/d.sock"
mkdir -p "$deep"

killDaemon
export NIX_DAEMON_SOCKET_PATH=$sock
startDaemon

out=$(nix --extra-experimental-features nix-command store ping 2>&1)
echo "$out" | grep -q 'Store URL' || fail "unexpected ping output: $out"

killDaemon
