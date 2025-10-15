#!/usr/bin/env bash

source common.sh

requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"
if ! command -p -v unshare; then skipTest "Need unshare"; fi
needLocalStore "The test uses --store always so we would just be bypassing the daemon"

TODO_NixOS

# shellcheck disable=SC2119
execUnshare <<EOF
  source common.sh

  # Avoid store dir being inside sandbox build-dir
  unset NIX_STORE_DIR

  setLocalStore () {
    export NIX_REMOTE=\$TEST_ROOT/\$1
    mkdir -p \$NIX_REMOTE
  }

  cmd=(nix-build ./hermetic.nix --arg busybox "$busybox" --arg seed 1 --no-out-link)

  # Fails with default setting
  setLocalStore store1
  expectStderr 1 "\${cmd[@]}" | grepQuiet "setgroups failed"

  # Fails with $(require-drop-supplementary-groups)
  setLocalStore store2
  NIX_CONFIG='require-drop-supplementary-groups = true' \
    expectStderr 1 "\${cmd[@]}" | grepQuiet "setgroups failed"

  # Works without $(require-drop-supplementary-groups)
  setLocalStore store3
  NIX_CONFIG='require-drop-supplementary-groups = false' \
    "\${cmd[@]}"
EOF
