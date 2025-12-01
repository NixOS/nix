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

# Only derivation-meta is filtered, not the other features: filtering derivation-meta
# and __meta must leave a derivation that hashes identically to one that never had them.
echo "Testing that only derivation-meta is filtered when other features are present..."
drvOther=$(nix-instantiate meta.nix -A metaWithOtherFeatures)
pathOther=$(nix-store -q "$drvOther")
pathOtherNoMeta=$(nix-store -q "$(nix-instantiate meta.nix -A otherFeaturesNoMeta)")
[[ "$pathOther" == "$pathOtherNoMeta" ]] || fail "Output path should be unaffected by __meta and derivation-meta when other features are present"

# Read the stored derivation back. `derivation show` re-parses the on-disk ATerm,
# which re-splices derivation-meta into the middle of the sorted requiredSystemFeatures
# list (kvm sorts after it) and then re-extracts it; if the splice produced an
# out-of-order list, extraction would reject it. The other features and __meta must
# survive the round-trip.
nix derivation show "$drvOther" | grepQuiet '"big-parallel"'
nix derivation show "$drvOther" | grepQuiet '"kvm"'
nix derivation show "$drvOther" | grepQuiet "With other features"

# requiredSystemFeatures must be sorted when using derivation-meta. This goes
# through `extractMeta` (ATerm path), which shares its check with `checkInvariants`.
echo "Testing that unsorted requiredSystemFeatures is rejected..."
expectStderr 1 nix-instantiate meta.nix -A metaWithUnsortedFeatures \
  | grepQuiet -F "has unsorted 'requiredSystemFeatures', which is not permitted when using 'derivation-meta'"

# A duplicate is reported as a duplicate, not as "unsorted", on the ATerm path too.
echo "Testing that duplicate requiredSystemFeatures is rejected as a duplicate..."
expectStderr 1 nix-instantiate meta.nix -A metaWithDuplicateFeatures \
  | grepQuiet -F "has a duplicate 'requiredSystemFeatures' entry 'big-parallel', which is not permitted when using 'derivation-meta'"

# A non-string requiredSystemFeatures entry is rejected while reading the list. This
# is general requiredSystemFeatures typing, not a derivation-meta rule, but the meta
# ATerm path reads the list (in extractMeta) so it must not choke on a bad element.
# Craft such a derivation on disk and read it back in.
echo "Testing that a non-string requiredSystemFeatures entry is rejected on parse..."
validMeta=$(nix-instantiate meta.nix -A metaDiff1)
sed 's/\[\\"derivation-meta\\"\]/[\\"derivation-meta\\",5]/' "$validMeta" > "$TEST_HOME/crafted.drv"
expectStderr 1 nix-store --add "$TEST_HOME/crafted.drv" \
  | grepQuiet -F "Expected JSON value to be of type 'string' but it is of type 'number': 5"

# meta only exists as part of structured attributes. `nix derivation add` accepts
# an arbitrary JSON derivation, so it can express meta without structuredAttrs -
# a state that would silently drop the meta on serialisation. Reject it.
echo "Testing that meta without structured attributes is rejected..."
traditionalDrv=$(nix-instantiate meta.nix -A withoutStructuredAttrs)
nix derivation show "$traditionalDrv" | jq '.derivations[] | .meta = { description: "oops" }' > "$TEST_HOME/meta-no-sa.json"
expectStderr 1 nix derivation add < "$TEST_HOME/meta-no-sa.json" \
  | grepQuiet "'meta' requires structured attributes"

# `nix derivation add` accepts arbitrary JSON, so a meta derivation's structured
# attributes could be given in a shape that `extractMeta` would never produce -
# one that `unparse`/`write` cannot faithfully reinject derivation-meta/__meta
# into. `checkInvariants` rejects such shapes up front, rather than relying on a
# later output-hash mismatch or an `extractMeta` failure when the derivation is
# re-read.
nix derivation show "$(nix-instantiate meta.nix -A metaWithOtherFeatures)" \
  | jq '.derivations[]' > "$TEST_HOME/meta-sa.json"

# Baseline: the clean shape produced by `derivation show` round-trips.
nix derivation add < "$TEST_HOME/meta-sa.json"

# The remaining checks use `--dry-run` deliberately: it validates the derivation
# (`checkInvariants`) but does not write it to the store. Without `--dry-run`
# these bad shapes happen to be rejected incidentally - by an output hash mismatch,
# or by `extractMeta` when the local store re-reads the just-written derivation -
# which would mask whether validation itself rejects them. `--dry-run` isolates the
# `checkInvariants` gate.

# requiredSystemFeatures must be sorted, otherwise splicing derivation-meta back in
# yields an unsorted list that Nix cannot read back.
echo "Testing that meta with unsorted requiredSystemFeatures is rejected..."
jq '.outputs |= map_values(del(.path)) | .structuredAttrs.requiredSystemFeatures = [ "kvm", "big-parallel" ]' \
  < "$TEST_HOME/meta-sa.json" \
  | expectStderr 1 nix derivation add --dry-run \
  | grepQuiet -F "derivation 'meta-test' has unsorted 'requiredSystemFeatures', which is not permitted when 'meta' is set"

# A sorted list with duplicates is reported as a duplicate, not as "unsorted":
# duplicates are a distinct condition, and reporting them accurately avoids
# telling a user to sort a list that is already sorted.
echo "Testing that meta with duplicate requiredSystemFeatures is rejected..."
jq '.outputs |= map_values(del(.path)) | .structuredAttrs.requiredSystemFeatures = [ "big-parallel", "big-parallel" ]' \
  < "$TEST_HOME/meta-sa.json" \
  | expectStderr 1 nix derivation add --dry-run \
  | grepQuiet -F "derivation 'meta-test' has a duplicate 'requiredSystemFeatures' entry 'big-parallel', which is not permitted when 'meta' is set"

# derivation-meta must not be listed explicitly; it is implied by meta and
# reinjected on serialisation, so an explicit entry would be duplicated.
echo "Testing that meta with an explicit derivation-meta feature is rejected..."
jq '.outputs |= map_values(del(.path)) | .structuredAttrs.requiredSystemFeatures = [ "big-parallel", "derivation-meta", "kvm" ]' \
  < "$TEST_HOME/meta-sa.json" \
  | expectStderr 1 nix derivation add --dry-run \
  | grepQuiet "must not list 'derivation-meta'"

# __meta is the internal encoding of the meta field and must not be set directly
# in the structured attributes alongside it.
echo "Testing that meta with an explicit __meta structured attribute is rejected..."
jq '.outputs |= map_values(del(.path)) | .structuredAttrs.__meta = { } ' \
  < "$TEST_HOME/meta-sa.json" \
  | expectStderr 1 nix derivation add --dry-run \
  | grepQuiet "'__meta'"

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

# Test that __meta doesn't leak into the builder
echo "Testing that __meta doesn't leak into builder..."
nix-build meta.nix -A metaNotInBuilder --no-out-link

