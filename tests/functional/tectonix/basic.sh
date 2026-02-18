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
if [[ ! "$zone_src" =~ ^${NIX_STORE_DIR:-/nix/store}/ ]]; then
    fail "Zone source should be a store path, got: $zone_src"
fi

# Test: Zone attribute set - verify individual attributes
zone_outpath=$(tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    '(builtins.unsafeTectonixInternalZone "//areas/tools/dev").outPath')
echo "Zone outPath: $zone_outpath"
[[ -n "$zone_outpath" ]] || fail "Zone should have outPath"

zone_treeSha=$(tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    '(builtins.unsafeTectonixInternalZone "//areas/tools/dev").treeSha')
echo "Zone treeSha: $zone_treeSha"
[[ -n "$zone_treeSha" ]] || fail "Zone should have treeSha"

zone_zonePath=$(tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    '(builtins.unsafeTectonixInternalZone "//areas/tools/dev").zonePath')
echo "Zone zonePath: $zone_zonePath"
[[ "$zone_zonePath" == "//areas/tools/dev" ]] || fail "Zone zonePath should be //areas/tools/dev, got: $zone_zonePath"

zone_dirty=$(tectonix_eval_json "$TEST_WORLD/.git" "$HEAD_SHA" \
    '(builtins.unsafeTectonixInternalZone "//areas/tools/dev").dirty')
echo "Zone dirty: $zone_dirty"

# Verify dirty is false (clean repo)
if [[ "$zone_dirty" == "true" ]]; then
    fail "Zone should not be dirty in clean repo"
fi

echo "Basic tests passed!"
