source common.sh

[[ -n $(type -p git) ]] || skipTest "no git"

repo=$TEST_ROOT/git

rm -rf $repo $TEST_HOME/.cache/nix

git init $repo
git -C $repo config user.email "foobar@example.com"
git -C $repo config user.name "Foobar"

echo utrecht > $repo/hello
touch $repo/.gitignore
git -C $repo add hello .gitignore
git -C $repo commit -m 'Bla1'

echo world > $repo/hello
git -C $repo commit -m 'Bla2' -a

treeHash=$(git -C $repo rev-parse HEAD:)

# Fetch the default branch.
path=$(nix eval --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; treeHash = \"$treeHash\"; }).outPath")
[[ $(cat $path/hello) = world ]]

# Submodules are fine with nar hashing the result
pathSub=$(nix eval --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; treeHash = \"$treeHash\"; submodules = true; }).outPath")
[[ "$path" = "$pathSub" ]]

# This might not work any more because of caching changes?
#
# # Check that we can substitute it from other places.
# nix copy --to file://$cacheDir $path
# nix-store --delete $path
# path2=$(nix eval --raw --expr "(builtins.fetchTree { type = \"git\"; url = file:///no-such-repo; treeHash = \"$treeHash\"; }).outPath" --substituters file://$cacheDir --option substitute true)
# [ $path2 = $path ]

# HEAD should be the same path and tree hash as tree
nix eval --impure --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; ref = \"HEAD\"; })"
treeHash2=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; ref = \"HEAD\"; }).treeHash")
[ $treeHash = $treeHash2 ]
path3=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; ref = \"HEAD\"; }).outPath")
[ $path3 = $path ]
caFromNix=$(nix path-info --json "$path" | jq -r ".[] | .ca")

# FIXME still using NAR hashing, should use git hashing
# test "fixed:git:sha1:$(nix hash convert --to nix32 "sha1:$treeHash")" = "$caFromNix"
