source common.sh

clearStore

if ! canUseSandbox; then exit; fi
if [[ ! $SHELL =~ /nix/store ]]; then exit; fi

chmod -R u+w $TEST_ROOT/store0 || true
chmod -R u+w $TEST_ROOT/store1 || true
rm -rf $TEST_ROOT/store0 $TEST_ROOT/store1

nix build -f build-hook.nix -o $TEST_ROOT/result --max-jobs 0 \
  --sandbox-paths /nix/store --sandbox-build-dir /build-tmp \
  --builders "$TEST_ROOT/store0; $TEST_ROOT/store1 - - 1 1 foo" \
  --system-features foo

outPath=$TEST_ROOT/result

cat $outPath/foobar | grep FOOBAR

# Ensure that input1 was built on store1 due to the required feature.
p=$(readlink -f $outPath/input-2)
(! nix path-info --store $TEST_ROOT/store0 --all | grep dependencies.builder1.sh)
nix path-info --store $TEST_ROOT/store1 --all | grep dependencies.builder1.sh
