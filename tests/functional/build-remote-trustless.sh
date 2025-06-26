# shellcheck shell=bash

# All variables should be defined externally by the scripts that source
# this, `set -u` will catch any that are forgotten.
# shellcheck disable=SC2154

requireSandboxSupport
requiresUnprivilegedUserNamespaces
[[ "$busybox" =~ busybox ]] || skipTest "no busybox"

unset NIX_STORE_DIR

remoteDir=$TEST_ROOT/remote

# Note: ssh{-ng}://localhost bypasses ssh. See tests/functional/build-remote.sh for
# more details.
nix-build "$file" -o "$TEST_ROOT/result" --max-jobs 0 \
  --arg busybox "$busybox" \
  --store "$TEST_ROOT/local" \
  --builders "$proto://localhost?remote-program=$prog&remote-store=${remoteDir}%3Fsystem-features=foo%20bar%20baz - - 1 1 foo,bar,baz"
