#!/usr/bin/env bash

source common.sh

TODO_NixOS

enableFeatures derivation-meta

# When the daemon doesn't support derivation-meta, the client should
# produce a clear error about missing support.
if ! isDaemonNewer "2.35"; then
    expectStderr 1 nix-instantiate meta.nix -A metaDiff1 \
      | grepQuiet "'derivation-meta', but the store"
    exit 0
fi

restartDaemon
clearStore

# Test the quotient property for derivation-meta
# See: https://nix.dev/manual/nix/latest/store/derivation/outputs/input-address#hash-quotient-drv
echo "Testing quotient property: same output path despite different __meta..."
path1=$(nix-store -q "$(nix-instantiate meta.nix -A metaDiff1)")
path2=$(nix-store -q "$(nix-instantiate meta.nix -A metaDiff2)")
[[ "$path1" == "$path2" ]] || fail "Output paths should be equal when only __meta differs"

# Derivation paths themselves should differ
echo "Testing that derivation paths differ when __meta differs..."
drv1=$(nix-instantiate meta.nix -A metaDiff1)
drv2=$(nix-instantiate meta.nix -A metaDiff2)
[[ "$drv1" != "$drv2" ]] || fail "Derivation paths should differ when __meta differs"

# Without derivation-meta system feature, __meta is NOT filtered
echo "Testing that __meta is NOT filtered without derivation-meta system feature..."
path3=$(nix-store -q "$(nix-instantiate meta.nix -A withoutSystemFeature)")
path4=$(nix-store -q "$(nix-instantiate meta.nix -A metaDiff1)")
[[ "$path3" != "$path4" ]] || fail "Output paths should differ when derivation-meta system feature is missing"

# Without structured attrs, __meta is just a regular env var
echo "Testing that __meta works without structured attributes..."
nix-instantiate meta.nix -A withoutStructuredAttrs

# Empty __meta should work
echo "Testing empty __meta..."
nix-instantiate meta.nix -A emptyMeta

# Unsorted requiredSystemFeatures without derivation-meta should be fine
echo "Testing that unsorted requiredSystemFeatures is allowed without derivation-meta..."
nix-instantiate meta.nix -A unsortedFeaturesNonMeta

# Unsorted requiredSystemFeatures should be rejected
# 1. Avoid unnecessary entropy.
# 2. Permit a clean data model. In practical terms, allow the derivation-meta
#    flag to be reinjected into the ATerm format for serialization without
#    having to remember its index in the original ATerm input.
echo "Testing that unsorted requiredSystemFeatures is rejected..."
expectStderr 1 nix-instantiate meta.nix -A unsortedFeatures \
  | grepQuiet "'requiredSystemFeatures' must be sorted when using 'derivation-meta'"

# Test that filtering removes requiredSystemFeatures entirely when it becomes empty
echo "Testing that empty requiredSystemFeatures is removed entirely..."
pathWithout=$(nix-store -q "$(nix-instantiate meta.nix -A withoutRequiredSystemFeatures)")
pathWithOnly=$(nix-store -q "$(nix-instantiate meta.nix -A withOnlyDerivationMeta)")
if [[ "$pathWithout" != "$pathWithOnly" ]]; then
    echo "ERROR: Output paths should match - when derivation-meta is the only system feature, filtering should remove the attribute entirely, not leave an empty array"
    exit 1
fi

# Test validation: __meta without derivation-meta system feature should fail at build time
echo "Testing validation: __meta without derivation-meta system feature should fail at build time..."
clearStore
if expectStderr 1 nix-build meta.nix -A withoutSystemFeature | grepQuiet "has '__meta' attribute but does not require 'derivation-meta' system feature"; then
    echo "Validation correctly rejected __meta without system feature"
else
    fail "Should have rejected __meta without derivation-meta system feature"
fi

# Test validation: derivation-meta system feature without experimental feature should fail
echo "Testing validation: derivation-meta without experimental feature should fail..."
clearStore
# Temporarily remove derivation-meta feature from daemon config
sed -i 's/ derivation-meta//' "${test_nix_conf?}"
restartDaemon
if expectStderr 1 nix-build meta.nix -A metaDiff1 --option experimental-features '' | grepQuiet "experimental Nix feature 'derivation-meta' is disabled"; then
    echo "Validation correctly rejected derivation-meta without experimental feature"
else
    fail "Should have rejected derivation-meta without experimental feature enabled"
fi
# Re-enable the experimental feature for remaining tests
enableFeatures derivation-meta
restartDaemon

# Test that a valid derivation with __meta actually builds successfully
echo "Testing that valid derivation with __meta builds successfully..."
clearStore
nix-build meta.nix -A metaDiff1 --no-out-link
echo "Valid derivation built successfully"

# Test that __meta doesn't leak into the builder
echo "Testing that __meta doesn't leak into builder..."
nix-build meta.nix -A metaNotInBuilder --no-out-link
echo "__meta correctly filtered from builder environment"

echo "All meta tests passed!"
