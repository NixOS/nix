#!/usr/bin/env bash
# Basic tectonix functionality tests

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Create test world
TEST_WORLD="$TEST_ROOT/world"
create_test_world "$TEST_WORLD"
HEAD_SHA=$(get_head_sha "$TEST_WORLD")

echo "Testing basic zone access..."

# Test: Manifest access
manifest=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalManifest')
echo "Manifest: $manifest"

# Verify manifest contains expected zones
echo "$manifest" | grepQuiet "//areas/tools/dev"
echo "$manifest" | grepQuiet "W-000001"

# Test: Inverted manifest
inverted=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalManifestInverted')
echo "Inverted manifest: $inverted"

echo "$inverted" | grepQuiet "W-000001"
echo "$inverted" | grepQuiet "//areas/tools/dev"

# Test: Tree SHA access
tree_sha=$(tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalTreeSha "//areas/tools/dev"')
echo "Tree SHA for //areas/tools/dev: $tree_sha"

# Verify SHA is 40 hex characters
if [[ ! "$tree_sha" =~ ^[0-9a-f]{40}$ ]]; then
    fail "Tree SHA should be 40 hex characters, got: $tree_sha"
fi

# Test: Zone source access
zone_src=$(tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneSrc "//areas/tools/dev"')
echo "Zone source path: $zone_src"

# Verify it's a store path
if [[ ! "$zone_src" =~ ^"$NIX_STORE_DIR" ]]; then
    fail "Zone source should be a store path, got: $zone_src"
fi

# Test: Zone attribute set
zone_root=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneRoot "//areas/tools/dev"')
echo "Zone root: $zone_root"

# Verify it exists in world tree
echo "$zone_root" | grepQuiet "$TEST_WORLD"
if [[ ! "$zone_root" =~ ^"$TEST_WORLD" ]]; then
    fail "Zone root should be in world tree"
fi

echo "Basic tests passed!"
