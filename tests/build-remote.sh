if ! canUseSandbox; then exit; fi
if ! [[ $busybox =~ busybox ]]; then exit; fi

unset NIX_STORE_DIR
unset NIX_STATE_DIR

function join_by { local d=$1; shift; echo -n "$1"; shift; printf "%s" "${@/#/$d}"; }

builders=(
  # system-features will automatically be added to the outer URL, but not inner
  # remote-store URL.
  "ssh://localhost?remote-store=$TEST_ROOT/machine1?system-features=foo - - 1 1 foo"
  "$TEST_ROOT/machine2 - - 1 1 bar"
  "ssh-ng://localhost?remote-store=$TEST_ROOT/machine3?system-features=baz - - 1 1 baz"
)

# Note: ssh://localhost bypasses ssh, directly invoking nix-store as a
# child process. This allows us to test LegacySSHStore::buildDerivation().
# ssh-ng://... likewise allows us to test RemoteStore::buildDerivation().
nix build -L -v -f $file -o $TEST_ROOT/result --max-jobs 0 \
  --arg busybox $busybox \
  --store $TEST_ROOT/machine0 \
  --builders "$(join_by '; ' "${builders[@]}")"

outPath=$(readlink -f $TEST_ROOT/result)

grep 'FOO BAR BAZ' $TEST_ROOT/machine0/$outPath

set -o pipefail

# Ensure that input1 was built on store1 due to the required feature.
nix path-info --store $TEST_ROOT/machine1 --all \
  | grep builder-build-remote-input-1.sh \
  | grep -v builder-build-remote-input-2.sh \
  | grep -v builder-build-remote-input-3.sh

# Ensure that input2 was built on store2 due to the required feature.
nix path-info --store $TEST_ROOT/machine2 --all \
  | grep -v builder-build-remote-input-1.sh \
  | grep builder-build-remote-input-2.sh \
  | grep -v builder-build-remote-input-3.sh

# Ensure that input3 was built on store3 due to the required feature.
nix path-info --store $TEST_ROOT/machine3 --all \
  | grep -v builder-build-remote-input-1.sh \
  | grep -v builder-build-remote-input-2.sh \
  | grep builder-build-remote-input-3.sh
