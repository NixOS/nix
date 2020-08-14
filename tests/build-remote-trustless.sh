if ! canUseSandbox; then exit; fi
if ! [[ $busybox =~ busybox ]]; then exit; fi

unset NIX_STORE_DIR
unset NIX_STATE_DIR

# Note: ssh{-ng}://localhost bypasses ssh. See tests/build-remote.sh for
# more details.
nix build -L -v -f $file -o $TEST_ROOT/result --max-jobs 0 \
  --arg busybox $busybox \
  --store $TEST_ROOT/local \
  --builders "$proto://localhost?remote-program=$prog&trusting=$trusting&remote-store=$TEST_ROOT/remote%3Fsystem-features=foo%20bar%20baz - - 1 1 foo,bar,baz"

outPath=$(readlink -f $TEST_ROOT/result)

grep 'FOO BAR BAZ' $TEST_ROOT/${subDir}/local${outPath}
