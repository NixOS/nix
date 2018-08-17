source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping Git tests"
    exit 99
fi

clearStore

repo=$TEST_ROOT/git

rm -rf $repo ${repo}-tmp $TEST_HOME/.cache/nix/gitv2

git init $repo
git -C $repo config user.email "foobar@example.com"
git -C $repo config user.name "Foobar"

echo utrecht > $repo/hello
touch $repo/.gitignore
git -C $repo add hello .gitignore
git -C $repo commit -m 'Bla1'
rev1=$(git -C $repo rev-parse HEAD)

echo world > $repo/hello
git -C $repo commit -m 'Bla2' -a
rev2=$(git -C $repo rev-parse HEAD)

# Fetch the default branch.
path=$(nix eval --raw "(builtins.fetchGit file://$repo).outPath")
[[ $(cat $path/hello) = world ]]

# In pure eval mode, fetchGit without a revision should fail.
[[ $(nix eval --raw "(builtins.readFile (fetchGit file://$repo + \"/hello\"))") = world ]]
(! nix eval --pure-eval --raw "(builtins.readFile (fetchGit file://$repo + \"/hello\"))")

# Fetch using an explicit revision hash.
path2=$(nix eval --raw "(builtins.fetchGit { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

# In pure eval mode, fetchGit with a revision should succeed.
[[ $(nix eval --pure-eval --raw "(builtins.readFile (fetchGit { url = file://$repo; rev = \"$rev2\"; } + \"/hello\"))") = world ]]

# Fetch again. This should be cached.
mv $repo ${repo}-tmp
path2=$(nix eval --raw "(builtins.fetchGit file://$repo).outPath")
[[ $path = $path2 ]]

[[ $(nix eval "(builtins.fetchGit file://$repo).revCount") = 2 ]]
[[ $(nix eval --raw "(builtins.fetchGit file://$repo).rev") = $rev2 ]]

# But with TTL 0, it should fail.
(! nix eval --tarball-ttl 0 "(builtins.fetchGit file://$repo)" -vvvvv)

# Fetching with a explicit hash should succeed.
path2=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchGit { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

path2=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchGit { url = file://$repo; rev = \"$rev1\"; }).outPath")
[[ $(cat $path2/hello) = utrecht ]]

mv ${repo}-tmp $repo

# Using a clean working tree should produce the same result.
path2=$(nix eval --raw "(builtins.fetchGit $repo).outPath")
[[ $path = $path2 ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
mkdir $repo/dir1 $repo/dir2
echo foo > $repo/dir1/foo
echo bar > $repo/bar
echo bar > $repo/dir2/bar
git -C $repo add dir1/foo
git -C $repo rm hello

path2=$(nix eval --raw "(builtins.fetchGit $repo).outPath")
[ ! -e $path2/hello ]
[ ! -e $path2/bar ]
[ ! -e $path2/dir2/bar ]
[ ! -e $path2/.git ]
[[ $(cat $path2/dir1/foo) = foo ]]

[[ $(nix eval --raw "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]

# ... unless we're using an explicit ref or rev.
path3=$(nix eval --raw "(builtins.fetchGit { url = $repo; ref = \"master\"; }).outPath")
[[ $path = $path3 ]]

path3=$(nix eval --raw "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path3 ]]

# Committing should not affect the store path.
git -C $repo commit -m 'Bla3' -a

path4=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchGit file://$repo).outPath")
[[ $path2 = $path4 ]]

# tarball-ttl should be ignored if we specify a rev
echo delft > $repo/hello
git -C $repo add hello
git -C $repo commit -m 'Bla4'
rev3=$(git -C $repo rev-parse HEAD)
nix eval --tarball-ttl 3600 "(builtins.fetchGit { url = $repo; rev = \"$rev3\"; })" >/dev/null

# Update 'path' to reflect latest master
path=$(nix eval --raw "(builtins.fetchGit file://$repo).outPath")

# Check behavior when non-master branch is used
git -C $repo checkout $rev2 -b dev
echo dev > $repo/hello

# File URI uses 'master' unless specified otherwise
path2=$(nix eval --raw "(builtins.fetchGit file://$repo).outPath")
[[ $path = $path2 ]]

# Using local path with branch other than 'master' should work when clean or dirty
path3=$(nix eval --raw "(builtins.fetchGit $repo).outPath")
# (check dirty-tree handling was used)
[[ $(nix eval --raw "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]

# Committing shouldn't change store path, or switch to using 'master'
git -C $repo commit -m 'Bla5' -a
path4=$(nix eval --raw "(builtins.fetchGit $repo).outPath")
[[ $(cat $path4/hello) = dev ]]
[[ $path3 = $path4 ]]

# Confirm same as 'dev' branch
path5=$(nix eval --raw "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = $path5 ]]


# Nuke the cache
rm -rf $TEST_HOME/.cache/nix/gitv2

# Try again, but without 'git' on PATH
NIX=$(command -v nix)
# This should fail
(! PATH= $NIX eval --raw "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath" )

# Try again, with 'git' available.  This should work.
path5=$(nix eval --raw "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = $path5 ]]
