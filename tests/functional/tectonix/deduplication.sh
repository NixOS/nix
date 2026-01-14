#!/usr/bin/env bash
# Test that same tree SHA across commits returns same path (deduplication)

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Create test world
TEST_WORLD="$TEST_ROOT/world"
create_test_world "$TEST_WORLD"

cd "$TEST_WORLD"

# Get first commit SHA
SHA1=$(git rev-parse HEAD)
echo "First commit: $SHA1"

# Make a commit that doesn't touch //areas/tools/dev
echo "Other content" >> README.md
git add README.md
git commit -m "Update README only"

# Get second commit SHA
SHA2=$(git rev-parse HEAD)
echo "Second commit: $SHA2"

cd - > /dev/null

# Get tree SHAs for both commits
tree_sha1=$(tectonix_eval "$TEST_WORLD/.git" "$SHA1" \
    'builtins.unsafeTectonixInternalTreeSha "//areas/tools/dev"')
echo "Tree SHA at commit 1: $tree_sha1"

tree_sha2=$(tectonix_eval "$TEST_WORLD/.git" "$SHA2" \
    'builtins.unsafeTectonixInternalTreeSha "//areas/tools/dev"')
echo "Tree SHA at commit 2: $tree_sha2"

# Tree SHAs should be identical since we didn't modify that zone
if [[ "$tree_sha1" != "$tree_sha2" ]]; then
    fail "Tree SHA should be same across commits for unchanged zone"
fi

# Get zone paths for both commits
path1=$(tectonix_eval "$TEST_WORLD/.git" "$SHA1" \
    'builtins.unsafeTectonixInternalZoneSrc "//areas/tools/dev"')
echo "Zone path at commit 1: $path1"

path2=$(tectonix_eval "$TEST_WORLD/.git" "$SHA2" \
    'builtins.unsafeTectonixInternalZoneSrc "//areas/tools/dev"')
echo "Zone path at commit 2: $path2"

# Should be same path due to tree SHA deduplication
if [[ "$path1" != "$path2" ]]; then
    fail "Expected same path for unchanged zone across commits"
fi

echo "Deduplication test passed!"
