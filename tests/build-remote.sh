source common.sh

clearStore

if ! canUseSandbox; then exit; fi
if ! [[ $busybox =~ busybox ]]; then exit; fi

chmod -R u+w $TEST_ROOT/machine0 || true
chmod -R u+w $TEST_ROOT/machine1 || true
chmod -R u+w $TEST_ROOT/machine2 || true
rm -rf $TEST_ROOT/machine0 $TEST_ROOT/machine1 $TEST_ROOT/machine2
rm -f $TEST_ROOT/result

unset NIX_STORE_DIR
unset NIX_STATE_DIR

# Note: ssh://localhost bypasses ssh, directly invoking nix-store as a
# child process. This allows us to test LegacySSHStore::buildDerivation().
nix build -L -v -f build-hook.nix -o $TEST_ROOT/result --max-jobs 0 \
  --arg busybox $busybox \
  --store $TEST_ROOT/machine0 \
  --builders "ssh://localhost?remote-store=$TEST_ROOT/machine1; $TEST_ROOT/machine2 - - 1 1 foo" \
  --system-features foo

outPath=$(readlink -f $TEST_ROOT/result)

cat $TEST_ROOT/machine0/$outPath | grep FOOBAR

# Ensure that input1 was built on store2 due to the required feature.
(! nix path-info --store $TEST_ROOT/machine1 --all | grep builder-build-remote-input-1.sh)
nix path-info --store $TEST_ROOT/machine2 --all | grep builder-build-remote-input-1.sh
