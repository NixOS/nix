#!/usr/bin/env bash

source common.sh

requireSandboxSupport

# Requires bug fix for daemon
requireDaemonNewerThan "2.35.0pre20260518"

# See https://github.com/NixOS/nix/issues/15846
#
# `slow` is a fixed-output derivation that never finishes (which simulates a
# slow derivation that loses the race), and `bad` is a fixed-output derivation
# whose *correct* store path is the same as `slow`'s *declared output*. A
# deadlock used to happen when:
#
# - `slow` locks the output path (A)
# - `bad` completes
# - `bad` locks the same output path as `slow` (B)
# - (Never gets to happen: the output of `bad` is moved to the store path)
#
# In this case, we should let (B) fail and just don't try to move the output of
# `bad`. This does mean that the output of `bad` is gone (if we don't pass
# `--keep-going`), but the situation still makes sense since it can be explained
# as `slow` being cancelled.

sandboxArgs=()

# Sandboxing is not required, but it makes the deadlock more likely to happen.
if canUseSandbox; then
  sandboxArgs+=(--option sandbox true)
  sandboxArgs+=(--option sandbox-build-dir /build-non-standard)

  # When using a separate test store, we need sandbox-paths to access
  # the system store. See the same check in build.sh.
  if ! isTestOnNixOS; then
      sandboxArgs+=(--option extra-sandbox-paths "/nix/store")
  fi

  fifoDir="$TEST_ROOT/sync-fifo"
  mkdir -p "$fifoDir"
  mkfifo "$fifoDir/fifo"
  chmod a+rw "$fifoDir/fifo"
  sandboxArgs+=(--option extra-sandbox-paths "/sync-fifo=$TEST_ROOT/sync-fifo")
fi

expectStderr 1 nix build -L --max-jobs 2 "${sandboxArgs[@]}" -f fixed-slow-vs-bad.nix slow bad \
  | grepQuiet "hash mismatch in fixed-output derivation"
