requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR
unset NIX_STATE_DIR

function join_by { local d=$1; shift; echo -n "$1"; shift; printf "%s" "${@/#/$d}"; }

EXTRA_SYSTEM_FEATURES=()
if [[ -n "${CONTENT_ADDRESSED-}" ]]; then
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

testPrintOutPath=$(nix build -L -v -f $file --no-link --print-out-paths --max-jobs 0 \
  --arg busybox $busybox \
  --store $TEST_ROOT/machine0 \
  --builders "$(join_by '; ' "${builders[@]}")"
)

[[ $testPrintOutPath =~ store.*build-remote ]]

# Ensure that input1 was built on store1 due to the required feature.
output=$(nix path-info --store $TEST_ROOT/machine1 --all)
echo "$output" | grepQuiet builder-build-remote-input-1.sh
echo "$output" | grepQuietInverse builder-build-remote-input-2.sh
echo "$output" | grepQuietInverse builder-build-remote-input-3.sh
unset output

# Ensure that input2 was built on store2 due to the required feature.
output=$(nix path-info --store $TEST_ROOT/machine2 --all)
echo "$output" | grepQuietInverse builder-build-remote-input-1.sh
echo "$output" | grepQuiet builder-build-remote-input-2.sh
echo "$output" | grepQuietInverse builder-build-remote-input-3.sh
unset output

# Ensure that input3 was built on store3 due to the required feature.
output=$(nix path-info --store $TEST_ROOT/machine3 --all)
echo "$output" | grepQuietInverse builder-build-remote-input-1.sh
echo "$output" | grepQuietInverse builder-build-remote-input-2.sh
echo "$output" | grepQuiet builder-build-remote-input-3.sh
unset output


for i in input1 input3; do
nix log --store $TEST_ROOT/machine0 --file "$file" --arg busybox $busybox passthru."$i" | grep hi-$i
done

# Behavior of keep-failed
out="$(nix-build 2>&1 failing.nix \
  --no-out-link \
  --builders "$(join_by '; ' "${builders[@]}")"  \
  --keep-failed \
  --store $TEST_ROOT/machine0 \
  -j0 \
  --arg busybox $busybox)" || true

[[ "$out" =~ .*"note: keeping build directory".* ]]

build_dir="$(grep "note: keeping build" <<< "$out" | sed -E "s/^(.*)note: keeping build directory '(.*)'(.*)$/\2/")"
[[ "foo" = $(<"$build_dir"/bar) ]]
