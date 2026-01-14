#!/usr/bin/env bash
# Test dirty zone detection

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Create test world
TEST_WORLD="$TEST_ROOT/world"
create_test_world "$TEST_WORLD"
HEAD_SHA=$(get_head_sha "$TEST_WORLD")

echo "Testing dirty zone detection..."

# First, verify zone is clean
zone_info=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    --option tectonix-checkout-path "$TEST_WORLD" \
    'builtins.unsafeTectonixInternalZone "//areas/tools/dev"')
echo "Clean zone info: $zone_info"

# Extract dirty status (should be false)
if echo "$zone_info" | grepQuiet '"dirty":true'; then
    fail "Zone should be clean before modification"
fi

# Modify a file in the zone
echo "Modified content" >> "$TEST_WORLD/areas/tools/dev/zone.nix"

# Now check dirty status
zone_info_dirty=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    --option tectonix-checkout-path "$TEST_WORLD" \
    'builtins.unsafeTectonixInternalZone "//areas/tools/dev"')
echo "Dirty zone info: $zone_info_dirty"

# dirty should now be true
if ! echo "$zone_info_dirty" | grepQuiet '"dirty":true'; then
    fail "Zone should be dirty after modification"
fi

# Check dirtyZones builtin
dirty_zones=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    --option tectonix-checkout-path "$TEST_WORLD" \
    'builtins.unsafeTectonixInternalDirtyZones')
echo "Dirty zones: $dirty_zones"

# Verify the modified zone appears as dirty
if ! echo "$dirty_zones" | grepQuiet "//areas/tools/dev"; then
    fail "Modified zone should appear in dirtyZones"
fi

# Verify an unmodified zone is not dirty
zone_info_clean=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    --option tectonix-checkout-path "$TEST_WORLD" \
    'builtins.unsafeTectonixInternalZone "//areas/tools/tec"')
echo "Unmodified zone info: $zone_info_clean"

if echo "$zone_info_clean" | grepQuiet '"dirty":true'; then
    fail "Unmodified zone should not be dirty"
fi

echo "Dirty zone tests passed!"
