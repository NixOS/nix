source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping Git tests"
    exit 99
fi

clearStore

repo=$TEST_ROOT/git

export _NIX_FORCE_HTTP=1

rm -rf $repo ${repo}-tmp $TEST_HOME/.cache/nix $TEST_ROOT/worktree $TEST_ROOT/shallow

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
git -C $repo worktree add $TEST_ROOT/worktree
echo hello >> $TEST_ROOT/worktree/hello
rev2=$(git -C $repo rev-parse HEAD)

# Fetch a worktree
unset _NIX_FORCE_HTTP
path0=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$TEST_ROOT/worktree).outPath")
path0_=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$TEST_ROOT/worktree; }).outPath")
[[ $path0 = $path0_ ]]
export _NIX_FORCE_HTTP=1
[[ $(tail -n 1 $path0/hello) = "hello" ]]

# Fetch the default branch.
path=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")
[[ $(cat $path/hello) = world ]]

# In pure eval mode, fetchGit without a revision should fail.
[[ $(nix eval --impure --raw --expr "builtins.readFile (fetchGit file://$repo + \"/hello\")") = world ]]
(! nix eval --raw --expr "builtins.readFile (fetchGit file://$repo + \"/hello\")")

# Fetch using an explicit revision hash.
path2=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

# In pure eval mode, fetchGit with a revision should succeed.
[[ $(nix eval --raw --expr "builtins.readFile (fetchGit { url = file://$repo; rev = \"$rev2\"; } + \"/hello\")") = world ]]

# Fetch again. This should be cached.
mv $repo ${repo}-tmp
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")
[[ $path = $path2 ]]

[[ $(nix eval --impure --expr "(builtins.fetchGit file://$repo).revCount") = 2 ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).rev") = $rev2 ]]

# Fetching with a explicit hash should succeed.
path2=$(nix eval --refresh --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

path2=$(nix eval --refresh --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev1\"; }).outPath")
[[ $(cat $path2/hello) = utrecht ]]

mv ${repo}-tmp $repo

# Using a clean working tree should produce the same result.
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $path = $path2 ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
mkdir $repo/dir1 $repo/dir2
echo foo > $repo/dir1/foo
echo bar > $repo/bar
echo bar > $repo/dir2/bar
git -C $repo add dir1/foo
git -C $repo rm hello

unset _NIX_FORCE_HTTP
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[ ! -e $path2/hello ]
[ ! -e $path2/bar ]
[ ! -e $path2/dir2/bar ]
[ ! -e $path2/.git ]
[[ $(cat $path2/dir1/foo) = foo ]]

[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]

# ... unless we're using an explicit ref or rev.
path3=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"master\"; }).outPath")
[[ $path = $path3 ]]

path3=$(nix eval --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path3 ]]

# Committing should not affect the store path.
git -C $repo commit -m 'Bla3' -a

path4=$(nix eval --impure --refresh --raw --expr "(builtins.fetchGit file://$repo).outPath")
[[ $path2 = $path4 ]]

nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; narHash = \"sha256-B5yIPHhEm0eysJKEsO7nqxprh9vcblFxpJG11gXJus1=\"; }).outPath" || status=$?
[[ "$status" = "102" ]]

path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; narHash = \"sha256-Hr8g6AqANb3xqX28eu1XnjK/3ab8Gv6TJSnkb1LezG9=\"; }).outPath")
[[ $path = $path5 ]]

# tarball-ttl should be ignored if we specify a rev
echo delft > $repo/hello
git -C $repo add hello
git -C $repo commit -m 'Bla4'
rev3=$(git -C $repo rev-parse HEAD)
nix eval --tarball-ttl 3600 --expr "builtins.fetchGit { url = $repo; rev = \"$rev3\"; }" >/dev/null

# Update 'path' to reflect latest master
path=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")

# Check behavior when non-master branch is used
git -C $repo checkout $rev2 -b dev
echo dev > $repo/hello

# File URI uses dirty tree unless specified otherwise
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")
[ $(cat $path2/hello) = dev ]

# Using local path with branch other than 'master' should work when clean or dirty
path3=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
# (check dirty-tree handling was used)
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]

# Committing shouldn't change store path, or switch to using 'master'
git -C $repo commit -m 'Bla5' -a
path4=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $(cat $path4/hello) = dev ]]
[[ $path3 = $path4 ]]

# Confirm same as 'dev' branch
path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = $path5 ]]


# Nuke the cache
rm -rf $TEST_HOME/.cache/nix

# Try again, but without 'git' on PATH. This should fail.
NIX=$(command -v nix)
(! PATH= $NIX eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath" )

# Try again, with 'git' available.  This should work.
path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = $path5 ]]

# Fetching a shallow repo shouldn't work by default, because we can't
# return a revCount.
git clone --depth 1 file://$repo $TEST_ROOT/shallow
(! nix eval --impure --raw --expr "(builtins.fetchGit { url = $TEST_ROOT/shallow; ref = \"dev\"; }).outPath")

# But you can request a shallow clone, which won't return a revCount.
path6=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow\"; ref = \"dev\"; shallow = true; }).outPath")
[[ $path3 = $path6 ]]
[[ $(nix eval --impure --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow\"; ref = \"dev\"; shallow = true; }).revCount or 123") == 123 ]]
