#!/usr/bin/env bash

# This test demonstrates that Nix's git fetcher incorrectly uses .gitmodules
# as the source of truth for submodules, rather than the git merkle dag.
#
# The real source of truth for which submodules exist is the tree object in
# git's merkle dag - entries with type "commit" (mode 160000) are submodules.
# The .gitmodules file is merely metadata (URL, branch hints, etc.).
#
# This test creates a submodule entry directly in the git tree without a
# corresponding .gitmodules entry. Currently Nix silently returns an empty
# directory for the submodule path. The ideal fix would be to fetch the
# submodule content, but without .gitmodules Nix doesn't know the URL.
# At minimum, Nix should error rather than silently succeeding with an
# empty directory.
#
# See: https://github.com/NixOS/nix/issues/15423

source ../common.sh

requireGit

clearStoreIfPossible

# Submodules can't be fetched locally by default.
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=protocol.file.allow
export GIT_CONFIG_VALUE_0=always

rootRepo=$TEST_ROOT/rootRepo
subRepo=$TEST_ROOT/subRepo

rm -rf "$rootRepo" "$subRepo" "$TEST_HOME"/.cache/nix

# Create the submodule repository
createGitRepo "$subRepo"
echo "content from submodule" > "$subRepo/file.txt"
git -C "$subRepo" add file.txt
git -C "$subRepo" commit -m "Initial commit in submodule"

# Create the root repository
createGitRepo "$rootRepo"
echo "content from root" > "$rootRepo/root.txt"
git -C "$rootRepo" add root.txt
git -C "$rootRepo" commit -m "Initial commit"

# Now we'll add a submodule to the tree WITHOUT using .gitmodules.
# In git, a submodule is simply a tree entry with mode 160000 (commit).
# We can create this directly using git's plumbing commands.

# First, let's create a proper submodule using `git submodule add`, just to test that also works.
git -C "$rootRepo" submodule add "$subRepo" sub
git -C "$rootRepo" commit -m "Add submodule"

rev_with_gitmodules=$(git -C "$rootRepo" rev-parse HEAD)

# Now remove .gitmodules but keep the submodule tree entry
# The submodule entry in the tree (mode 160000) is the real source of truth
git -C "$rootRepo" rm .gitmodules
git -C "$rootRepo" commit -m "Remove .gitmodules but keep submodule in tree"

rev_without_gitmodules=$(git -C "$rootRepo" rev-parse HEAD)

# Verify the tree still has the submodule entry (mode 160000 = commit)
git -C "$rootRepo" ls-tree HEAD | grep -q "^160000 commit"

# Helper to probe submodule state using Nix builtins
checkSubmodule() {
    local rev="$1"
    nix eval --impure --json --expr \
        "import $(dirname "${BASH_SOURCE[0]}")/submodule-without-gitmodules.nix { url = \"file://$rootRepo\"; rev = \"$rev\"; }"
}

# Verify .gitmodules case works: submodule should exist with content
result=$(checkSubmodule "$rev_with_gitmodules")

[[ "$(echo "$result" | jq -r '.exists')" == "true" ]] || fail "With .gitmodules: sub should exist"
[[ "$(echo "$result" | jq -r '.type')" == "directory" ]] || fail "With .gitmodules: sub should be a directory"
[[ "$(echo "$result" | jq -r '.hasContent')" == "true" ]] || fail "With .gitmodules: sub should have content"

# Check behavior without .gitmodules
result=$(checkSubmodule "$rev_without_gitmodules")

withoutGitmodules_exists=$(echo "$result" | jq -r '.exists')
withoutGitmodules_hasContent=$(echo "$result" | jq -r '.hasContent')

# BUG: Nix should either:
# 1. Fetch the submodule content (ideal, but requires knowing the URL), or
# 2. Error because the submodule commit is missing and can't be fetched
#
# Currently it silently succeeds with an empty directory, which is wrong.

if [[ "$withoutGitmodules_hasContent" == "true" ]]; then
    # Ideal fix: submodule was fetched correctly
    echo "FIXED (ideal): Submodule was fetched correctly from merkle dag" >&2
    echo "Please update this test to expect success." >&2
    exit 1
elif [[ "$withoutGitmodules_exists" == "false" ]]; then
    # Acceptable fix: submodule entry doesn't exist
    echo "FIXED (acceptable): Submodule directory not created" >&2
    echo "Please update this test to expect this behavior." >&2
    exit 1
else
    # Current buggy behavior: empty directory created silently
    # TODO: change to a proper expected failure once Meson >= 1.11.0 is required
    # (for should_fail with custom exit code support)
    skipTest "Submodule without .gitmodules silently creates empty directory (known bug)"
fi
