source common.sh

requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"
if ! command -p -v unshare; then skipTest "Need unshare"; fi
needLocalStore "The test uses --store always so we would just be bypassing the daemon"

unshare --mount --map-root-user bash <<EOF
  source common.sh

  # Avoid store dir being inside sandbox build-dir
  unset NIX_STORE_DIR
  unset NIX_STATE_DIR

  setLocalStore () {
    export NIX_REMOTE=\$TEST_ROOT/\$1
    mkdir -p \$NIX_REMOTE
  }

  cmd=(nix-build ./hermetic.nix --arg busybox "$busybox" --arg seed 1 --no-out-link)

  # Fails with default setting
  # TODO better error
  setLocalStore store1
  expectStderr 1 "\${cmd[@]}" | grepQuiet "unable to start build process"

  # Fails with `require-drop-supplementary-groups`
  # TODO better error
  setLocalStore store2
  NIX_CONFIG='require-drop-supplementary-groups = true' \
    expectStderr 1 "\${cmd[@]}" | grepQuiet "unable to start build process"

  # Works without `require-drop-supplementary-groups`
  setLocalStore store3
  NIX_CONFIG='require-drop-supplementary-groups = false' \
    "\${cmd[@]}"
EOF
