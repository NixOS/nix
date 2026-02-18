#!/usr/bin/env bash
# Test dirty zone detection

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Create test world
TEST_WORLD="$TEST_ROOT/world"
create_test_world "$TEST_WORLD"
HEAD_SHA=$(get_head_sha "$TEST_WORLD")

echo "Testing dirty zone detection..."

# First, verify zone is clean
zone_is_dirty=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneIsDirty "//areas/tools/dev"' \
    --option tectonix-checkout-path "$TEST_WORLD")
echo "Clean zone status: $zone_is_dirty"

# Extract dirty status (should be false)
if echo "$zone_is_dirty" | grepQuiet 'true'; then
    fail "Zone should be clean before modification"
fi

# Modify a file in the zone
echo "Modified content" >> "$TEST_WORLD/areas/tools/dev/zone.nix"

# Now check dirty status
zone_is_dirty_modified=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneIsDirty "//areas/tools/dev"' \
    --option tectonix-checkout-path "$TEST_WORLD")
echo "Dirty zone status: $zone_is_dirty_modified"

# dirty should now be true
if ! echo "$zone_is_dirty_modified" | grepQuiet 'true'; then
    fail "Zone should be dirty after modification"
fi

# Check dirtyZones builtin
dirty_zones=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalDirtyZones' \
    --option tectonix-checkout-path "$TEST_WORLD")
echo "Dirty zones: $dirty_zones"

# Verify the modified zone appears as dirty
if ! echo "$dirty_zones" | grepQuiet "//areas/tools/dev"; then
    fail "Modified zone should appear in dirtyZones"
fi

# Verify an unmodified zone is not dirty
zone_is_dirty_unmodified=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneIsDirty "//areas/tools/tec"' \
    --option tectonix-checkout-path "$TEST_WORLD")
echo "Unmodified zone status: $zone_is_dirty_unmodified"

if echo "$zone_is_dirty_unmodified" | grepQuiet 'true'; then
    fail "Unmodified zone should not be dirty"
fi

echo "Dirty zone tests passed!"
