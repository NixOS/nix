#!/usr/bin/env bash

source common.sh

requireGit

clearStoreIfPossible

repo="$TEST_ROOT/git"

rm -rf "$repo" "${repo}-tmp" "$TEST_HOME/.cache/nix"

git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"

echo foo > "$repo/blob"
mkdir "$repo/tree"
echo bar > "$repo/tree/blob"
git -C "$repo" add blob tree
git -C "$repo" commit -m 'Bla1'

rev=$(git -C $repo rev-parse HEAD)
blobrev=$(git -C $repo rev-parse HEAD:blob)
treerev=$(git -C $repo rev-parse HEAD:tree)

git -C $repo update-ref refs/blob $blobrev
git -C $repo update-ref refs/tree $treerev

git -C $repo tag -a blobtag -m "annotated tag" $blobrev
git -C $repo tag -a treetag -m "annotated tag" $treerev

# Fetch by hash
nix-instantiate --eval -E "builtins.readFile (builtins.fetchGit { url = file://$repo; rev = \"$blobrev\"; shallow = true; }) == \"foo\n\""
nix-instantiate --eval -E "builtins.readFile (builtins.fetchGit { url = file://$repo; rev = \"$treerev\"; shallow = true; } + \"/blob\") == \"bar\n\""

# Fetch by ref
nix-instantiate --eval -E "builtins.readFile (builtins.fetchGit { url = file://$repo; ref = \"refs/blob\"; shallow = true; }) == \"foo\n\""
nix-instantiate --eval -E "builtins.readFile (builtins.fetchGit { url = file://$repo; ref = \"refs/tree\"; shallow = true; } + \"/blob\") == \"bar\n\""

# Fetch by annotated tag
nix-instantiate --eval -E "builtins.readFile (builtins.fetchGit { url = file://$repo; ref = \"refs/tags/blobtag\"; shallow = true; }) == \"foo\n\""
nix-instantiate --eval -E "builtins.readFile (builtins.fetchGit { url = file://$repo; ref = \"refs/tags/treetag\"; shallow = true; } + \"/blob\") == \"bar\n\""

# fetchGit attributes
expectedAttrs="{ narHash = \"sha256-QvtAMbUl/uvi+LCObmqOhvNOapHdA2raiI4xG5zI5pA=\"; rev = \"$blobrev\"; shortRev = \"${blobrev:0:7}\"; submodules = false; }"
result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchGit { url = file://$repo; rev = \"$blobrev\"; shallow = true; }) [\"outPath\"]")
[[ "$result" = "$expectedAttrs" ]]

expectedAttrs="{ narHash = \"sha256-R/LfkvSLnUzdPeKhbQ6lGFpSfLdKvDw3LLicN46rUR4=\"; rev = \"$treerev\"; shortRev = \"${treerev:0:7}\"; submodules = false; }"
result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchGit { url = file://$repo; rev = \"$treerev\"; shallow = true; }) [\"outPath\"]")
[[ "$result" = "$expectedAttrs" ]]

# fetchTree attributes
expectedAttrs="{ narHash = \"sha256-QvtAMbUl/uvi+LCObmqOhvNOapHdA2raiI4xG5zI5pA=\"; rev = \"$blobrev\"; shortRev = \"${blobrev:0:7}\"; submodules = false; }"
result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchTree { type = \"git\"; url = file://$repo; rev = \"$blobrev\"; shallow = true; }) [\"outPath\"]")
[[ "$result" = "$expectedAttrs" ]]

expectedAttrs="{ narHash = \"sha256-R/LfkvSLnUzdPeKhbQ6lGFpSfLdKvDw3LLicN46rUR4=\"; rev = \"$treerev\"; shortRev = \"${treerev:0:7}\"; submodules = false; }"
result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchTree { type = \"git\"; url = file://$repo; rev = \"$treerev\"; shallow = true; }) [\"outPath\"]")
[[ "$result" = "$expectedAttrs" ]]
