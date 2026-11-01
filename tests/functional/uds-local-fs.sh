#!/usr/bin/env bash

source common.sh

TODO_NixOS

# A daemon that serves a store the client cannot see: the store objects
# live under another root, so they are missing from the client's store
# directory, as for a daemon in a virtual machine or a container.
hiddenRoot="$TEST_ROOT/hidden-root"
socket="$TEST_ROOT/hidden-daemon.sock"
NIX_DAEMON_SOCKET_PATH="$socket" PATH=$DAEMON_PATH \
    nix --store "local?root=$hiddenRoot" daemon 2>>"$TEST_ROOT/hidden-daemon-log" &
hiddenDaemonPid=$!
# Chain, not replace: with NIX_DAEMON_PACKAGE, common.sh sets `trap killDaemon EXIT`,
# and a test daemon left running keeps the test's stdout open, so the run never ends.
trap 'kill "$hiddenDaemonPid" 2>/dev/null || true; killDaemon' EXIT
for ((i = 0; i < 100; i++)); do
    [[ -S $socket ]] && break
    sleep 0.1
done
[[ -S $socket ]]

echo hello >"$TEST_ROOT/hello.txt"
path=$(nix store add-file --store "unix://$socket" "$TEST_ROOT/hello.txt")
[[ ! -e $path ]]

# By default the client reads store objects from its own file system.
expectStderr 1 nix store cat --store "unix://$socket" "$path" | grepQuiet "$path"

# With local-fs=false it reads them over the socket.
[[ "$(nix store cat --store "unix://$socket?local-fs=false" "$path")" = hello ]]
nix store dump-path --store "unix://$socket?local-fs=false" "$path" >"$TEST_ROOT/hello.nar"
[[ "$(nix nar cat "$TEST_ROOT/hello.nar" /)" = hello ]]
