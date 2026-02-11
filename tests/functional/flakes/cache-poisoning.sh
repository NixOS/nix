#!/usr/bin/env bash
source ./common.sh
TODO_NixOS
requireGit

clearStore
rm -rf "$TEST_HOME"/.cache "$TEST_HOME"/.config

# Force all commits to use the same timestamp so that lastModified is
# identical for both revisions, reproducing the exact NAR hash mismatch
# error seen in CI (rather than a lastModified mismatch).
export GIT_COMMITTER_DATE="2000-01-01T00:00:00+0000"
export GIT_AUTHOR_DATE="2000-01-01T00:00:00+0000"

# Create a git repo to use as a flake input.
depDir="$TEST_ROOT/dep"
createGitRepo "$depDir" ""
cat > "$depDir/flake.nix" <<EOF
{
  outputs = { self }: { expr = "rev1"; };
}
EOF
git -C "$depDir" add flake.nix
git -C "$depDir" commit -m 'rev1'

# Create a consumer flake that depends on it.
consumerDir="$TEST_ROOT/consumer"
createGitRepo "$consumerDir" ""
cat > "$consumerDir/flake.nix" <<EOF
{
  inputs.dep.url = "git+file://$depDir";
  outputs = { self, dep }: { expr = dep.expr; };
}
EOF
git -C "$consumerDir" add flake.nix
git -C "$consumerDir" commit -m 'Initial'

# Step 1: Lock correctly — populates store with rev1 content.
nix flake lock "$consumerDir"
git -C "$consumerDir" add flake.lock
git -C "$consumerDir" commit -m 'Lock'

# Capture the narHash for rev1.
oldNarHash=$(jq -r '.nodes.dep.locked.narHash' "$consumerDir/flake.lock")

# Evaluate to ensure rev1 content is in the store and cached.
[[ $(nix eval "$consumerDir#expr") = '"rev1"' ]]

# Create a new commit in dep (same timestamp → same lastModified).
cat > "$depDir/flake.nix" <<EOF
{
  outputs = { self }: { expr = "rev2"; };
}
EOF
git -C "$depDir" add flake.nix
git -C "$depDir" commit -m 'rev2'
rev2=$(git -C "$depDir" rev-parse HEAD)

# Step 2: Compute the correct narHash for rev2 by exporting its git tree
# to a clean directory (avoiding nix store/cache contamination).
exportDir="$TEST_ROOT/export"
rm -rf "$exportDir"
git -C "$depDir" archive --format=tar HEAD | (mkdir -p "$exportDir" && tar -xf - -C "$exportDir")
correctNarHash=$(nix hash path --type sha256 --sri "$exportDir")

# Sanity: the correct narHash must differ from the old one.
[[ "$oldNarHash" != "$correctNarHash" ]]

# Step 3: Manually edit consumer lock to new rev but keep OLD narHash (the bug trigger).
jq --arg rev "$rev2" '.nodes.dep.locked.rev = $rev' \
  "$consumerDir/flake.lock" > "$consumerDir/flake.lock.tmp"
mv "$consumerDir/flake.lock.tmp" "$consumerDir/flake.lock"
git -C "$consumerDir" add flake.lock
git -C "$consumerDir" commit -m 'Bad manual edit'

# Step 4: Evaluate with poisoned lock — silently serves old content, poisoning
# the fetchToStore cache: rev2_fingerprint → old_store_path.
nix eval "$consumerDir#expr" || true

# Step 5: Construct a correct lock file with rev2 + correct narHash.
jq --arg rev "$rev2" --arg narHash "$correctNarHash" \
  '.nodes.dep.locked.rev = $rev | .nodes.dep.locked.narHash = $narHash' \
  "$consumerDir/flake.lock" > "$consumerDir/flake.lock.tmp"
mv "$consumerDir/flake.lock.tmp" "$consumerDir/flake.lock"
git -C "$consumerDir" add flake.lock
git -C "$consumerDir" commit -m 'Correct lock'

# Step 6: Evaluate with correct lock — MUST NOT fail with narHash mismatch.
# BUG: Before fix, fails with:
#   error: NAR hash mismatch in input '...', expected '...' but got '...'
# because the poisoned fetchToStore cache maps rev2_fingerprint → old_store_path.
# FIXED: After fix, the fast-path cache key uses narHash (not rev fingerprint),
# so the poisoned entry is keyed under old_narHash and doesn't collide.
result=$(nix eval "$consumerDir#expr")
[[ "$result" = '"rev2"' ]]
