if ! canUseSandbox; then exit 99; fi
if ! [[ $busybox =~ busybox ]]; then exit 99; fi

unset NIX_STORE_DIR
unset NIX_STATE_DIR

function join_by { local d=$1; shift; echo -n "$1"; shift; printf "%s" "${@/#/$d}"; }

EXTRA_SYSTEM_FEATURES=()
if [[ -n "$CONTENT_ADDRESSED" ]]; then
    EXTRA_SYSTEM_FEATURES=("ca-derivations")
fi

builders=(
  # system-features will automatically be added to the outer URL, but not inner
  # remote-store URL.
  "ssh://localhost?remote-store=$TEST_ROOT/machine1?system-features=$(join_by "%20" foo ${EXTRA_SYSTEM_FEATURES[@]}) - - 1 1 $(join_by "," foo ${EXTRA_SYSTEM_FEATURES[@]})"
  "$TEST_ROOT/machine2 - - 1 1 $(join_by "," bar ${EXTRA_SYSTEM_FEATURES[@]})"
  "ssh-ng://localhost?remote-store=$TEST_ROOT/machine3?system-features=$(join_by "%20" baz ${EXTRA_SYSTEM_FEATURES[@]}) - - 1 1 $(join_by "," baz ${EXTRA_SYSTEM_FEATURES[@]})"
)

chmod -R +w $TEST_ROOT/machine* || true
rm -rf $TEST_ROOT/machine* || true

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


# Temporarily disabled because of https://github.com/NixOS/nix/issues/6209
if [[ -z "$CONTENT_ADDRESSED" ]]; then
  for i in input1 input3; do
    nix log --store $TEST_ROOT/machine0 --file "$file" --arg busybox $busybox passthru."$i" | grep hi-$i
  done
fi

# Behavior of keep-failed
out="$(nix-build 2>&1 failing.nix \
  --builders "$(join_by '; ' "${builders[@]}")"  \
  --keep-failed \
  --store $TEST_ROOT/machine0 \
  -j0 \
  --arg busybox $busybox)" || true

[[ "$out" =~ .*"note: keeping build directory".* ]]

build_dir="$(grep "note: keeping build" <<< "$out" | sed -E "s/^(.*)note: keeping build directory '(.*)'(.*)$/\2/")"
[[ "foo" = $(<"$build_dir"/bar) ]]
