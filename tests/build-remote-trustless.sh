requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"

unset NIX_STORE_DIR
unset NIX_STATE_DIR

remoteDir=$TEST_ROOT/remote

# Note: ssh{-ng}://localhost bypasses ssh. See tests/build-remote.sh for
# more details.
nix-build $file -o $TEST_ROOT/result --max-jobs 0 \
  --arg busybox $busybox \
  --store $TEST_ROOT/local \
  --builders "$proto://localhost?remote-program=$prog&remote-store=${remoteDir}%3Fsystem-features=foo%20bar%20baz - - 1 1 foo,bar,baz"
