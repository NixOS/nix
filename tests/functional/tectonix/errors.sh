#!/usr/bin/env bash
# Error handling tests for tectonix builtins

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Create test world
TEST_WORLD="$TEST_ROOT/world"
create_test_world "$TEST_WORLD"
HEAD_SHA=$(get_head_sha "$TEST_WORLD")

echo "Testing error handling..."

# Test: Missing git-dir setting
echo "Testing missing git-dir..."
expect_failure nix eval --json \
    --extra-experimental-features 'nix-command' \
    --option tectonix-git-sha "$HEAD_SHA" \
    --expr 'builtins.unsafeTectonixInternalManifest'

# Test: Invalid SHA
echo "Testing invalid SHA..."
expect_failure nix eval --json \
    --extra-experimental-features 'nix-command' \
    --option tectonix-git-dir "$TEST_WORLD/.git" \
    --option tectonix-git-sha "0000000000000000000000000000000000000000" \
    --expr 'builtins.unsafeTectonixInternalManifest'

# Test: Missing git-sha
echo "Testing missing git-sha..."
expect_failure nix eval --json \
    --extra-experimental-features 'nix-command' \
    --option tectonix-git-dir "$TEST_WORLD/.git" \
    --expr 'builtins.unsafeTectonixInternalManifest'

# Test: Non-zone path (parent of zone)
echo "Testing non-zone path (parent)..."
expect_failure tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneSrc "//areas/tools"'

# Test: Non-zone path (subpath of zone)
echo "Testing non-zone path (subpath)..."
expect_failure tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneSrc "//areas/tools/dev/subdir"'

# Test: Non-existent path
echo "Testing non-existent path..."
expect_failure tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalZoneSrc "//does/not/exist"'

# Test: Invalid tree SHA for __unsafeTectonixInternalTree
echo "Testing invalid tree SHA..."
expect_failure tectonix_eval "$TEST_WORLD/.git" "$HEAD_SHA" \
    'builtins.unsafeTectonixInternalTree "0000000000000000000000000000000000000000"'

# Test: Tree access works without git SHA

echo "Testing tree access without git SHA..."
TREE_SHA=$(git -C "$TEST_WORLD" rev-parse HEAD^{tree})
nix eval --raw \
    --extra-experimental-features 'nix-command' \
    --option tectonix-git-dir "$TEST_WORLD/.git" \
    --expr "builtins.unsafeTectonixInternalTree \"$TREE_SHA\"" > /dev/null

# Test: Non-existent git directory
echo "Testing non-existent git directory..."
expect_failure nix eval --json \
    --extra-experimental-features 'nix-command' \
    --option tectonix-git-dir "/nonexistent/path/.git" \
    --option tectonix-git-sha "$HEAD_SHA" \
    --expr 'builtins.unsafeTectonixInternalManifest'

echo "Error handling tests passed!"
