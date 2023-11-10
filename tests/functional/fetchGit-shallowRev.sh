source common.sh

requireGit

clearStore

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./git

export _NIX_FORCE_HTTP=1

rm -rf $TEST_HOME/.cache/nix $TEST_ROOT/shallow

git init $repo
git -C $repo config user.email "foobar@example.com"
git -C $repo config user.name "Foobar"

echo utrecht > $repo/hello
touch $repo/.gitignore
touch $repo/not-exported-file
echo "/not-exported-file export-ignore" >> $repo/.gitattributes
git -C $repo add hello not-exported-file .gitignore .gitattributes
git -C $repo commit -m 'Bla1'
rev1=$(git -C $repo rev-parse HEAD)

echo world > $repo/hello
git -C $repo commit -m 'Bla2' -a
rev2=$(git -C $repo rev-parse HEAD)


# Fetch local repo using shallowRev
path0=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev1\"; shallowRev = true; name = \"test1\"; }).outPath")
[[ $(cat $path0/hello) = utrecht ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev1\"; shallowRev = true; name = \"test1\"; }).rev") = $rev1 ]]

# Ensure .gitattributes is respected
[[ ! -e $path0/not-exported-file ]]

# Fetch local shallow repo using shallowRev
git clone --depth 1 file://$repo $TEST_ROOT/shallow
path1=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$TEST_ROOT/shallow; rev = \"$rev2\"; shallowRev = true; name = \"test2\"; }).outPath")
[[ $(cat $path1/hello) = world ]]

# Ensure that fetching a non-existing revision fails
# (since /shallow is a truncated repo, it does not contain $rev1)
out=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$TEST_ROOT/shallow; rev = \"$rev1\"; shallowRev = true; name = \"test3\"; }).outPath" 2>&1) || status=$?
[[ $status == 1 ]]
# The error message is not very spcific in this case, but what matters is that it fails properly
[[ $out =~ 'Could not fetch single revision of Git repository' ]]
