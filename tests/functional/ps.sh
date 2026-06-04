#!/usr/bin/env bash

source common.sh

TODO_NixOS

needLocalStore "the active-builds directory is per-store"

clearStore

# Two FIFOs: the builder writes to "started" once it is running, and
# reads from "finish" to know when to exit.
started=$TEST_ROOT/nix-ps-started.fifo
finish=$TEST_ROOT/nix-ps-finish.fifo
mkfifo "$started"
mkfifo "$finish"

# Start a build in the background that blocks until we tell it to finish.
nix-build --max-silent-time 60 -o "$TEST_ROOT/result" -E "
  with import ${config_nix};
  mkDerivation {
    name = \"nix-ps-test\";
    buildCommand = ''
      sleep 600 > /dev/null 2>&1 &
      echo started > $started
      read line < $finish
      mkdir \$out
    '';
  }" >"$TEST_ROOT/nix-ps-build.out" 2>&1 &
buildPid=$!

# Wait for the build to signal that it has started.
read -r _ < "$started"

# The build is now blocked on $finish. Check that `nix ps` sees it and
# the backgrounded `sleep 600` child process.
out=$(nix ps)
echo "$out"
echo "$out" | grepQuiet 'nix-ps-test.drv'
echo "$out" | grepQuiet 'sleep 600'

# Check the JSON output as well.
json=$(nix ps --json)
echo "$json" | jq -e 'length == 1' >/dev/null
echo "$json" | jq -e '.[0].derivation | test("nix-ps-test\\.drv$")' >/dev/null
echo "$json" | jq -e '.[0].mainPid | type == "number"' >/dev/null
echo "$json" | jq -e '[.[0].processes[].argv | join(" ")] | any(. | test("bash"))' >/dev/null
echo "$json" | jq -e '[.[0].processes[].argv | join(" ")] | any(. | test("sleep 600"))' >/dev/null
echo "$json" | jq -e '.[0].processes | length == 2' >/dev/null

# Release the build and wait for it to finish successfully.
echo "done" > "$finish"
wait "$buildPid"

# After the build finishes, `nix ps` should be empty again.
nix ps --json | jq -e 'length == 0' >/dev/null
