#!/usr/bin/env bash

# shellcheck source=common.sh
source common.sh

requireGit

# Create a test repo with multiple commits for all our tests
createGitRepo "$TEST_ROOT/shallow-parent"

# Add several commits to have history
echo "{ outputs = _: {}; }" > "$TEST_ROOT/shallow-parent/flake.nix"
echo "" > "$TEST_ROOT/shallow-parent/file.txt"
git -C "$TEST_ROOT/shallow-parent" add file.txt flake.nix
git -C "$TEST_ROOT/shallow-parent" commit -m "First commit"

echo "second" > "$TEST_ROOT/shallow-parent/file.txt"
git -C "$TEST_ROOT/shallow-parent" commit -m "Second commit" -a

echo "third" > "$TEST_ROOT/shallow-parent/file.txt"
git -C "$TEST_ROOT/shallow-parent" commit -m "Third commit" -a

# Add a branch for testing ref fetching
git -C "$TEST_ROOT/shallow-parent" checkout -b dev
echo "branch content" > "$TEST_ROOT/shallow-parent/branch-file.txt"
git -C "$TEST_ROOT/shallow-parent" add branch-file.txt
git -C "$TEST_ROOT/shallow-parent" commit -m "Branch commit"

# Make a shallow clone (depth=1)
git clone --depth 1 "file://$TEST_ROOT/shallow-parent" "$TEST_ROOT/shallow-clone"

# Test 1: Fetching a shallow repo shouldn't work by default, because we can't
# return a revCount.
(! nix eval --impure --raw --expr "(builtins.fetchGit { url = \"$TEST_ROOT/shallow-clone\"; ref = \"dev\"; }).outPath")

# Test 2: But you can request a shallow clone, which won't return a revCount.
path=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow-clone\"; ref = \"dev\"; shallow = true; }).outPath")
# Verify file from dev branch exists
[[ -f "$path/branch-file.txt" ]]
# Verify revCount is missing
[[ $(nix eval --impure --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow-clone\"; ref = \"dev\"; shallow = true; }).revCount or 123") == 123 ]]

# Test 3: Check unlocked input error message
expectStderr 1 nix eval --expr 'builtins.fetchTree { type = "git"; url = "file:///foo"; }' | grepQuiet "'fetchTree' doesn't fetch unlocked input"

# Test 4: Regression test for revCount in worktrees derived from shallow clones
# Add a worktree to the shallow clone
git -C "$TEST_ROOT/shallow-clone" worktree add "$TEST_ROOT/shallow-worktree"

# Prior to the fix, this would error out because of the shallow clone's
# inability to find parent commits. Now it should return an error.
if nix eval --impure --expr "(builtins.fetchGit { url = \"file://$TEST_ROOT/shallow-worktree\"; }).revCount" 2>/dev/null; then
    echo "fetchGit unexpectedly succeeded on shallow clone" >&2
    exit 1
fi

# Also verify that fetchTree fails similarly
if nix eval --impure --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow-worktree\"; }).revCount" 2>/dev/null; then
    echo "fetchTree unexpectedly succeeded on shallow clone" >&2
    exit 1
fi

# Verify that we can shallow fetch the worktree
git -C "$TEST_ROOT/shallow-worktree" rev-list --count HEAD >/dev/null
nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$TEST_ROOT/shallow-worktree\"; shallow = true; }).rev"

# Normal flake operation work because they use shallow by default
pushd "$TEST_ROOT/shallow-worktree"
nix flake metadata
popd
