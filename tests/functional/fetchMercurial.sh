source common.sh

[[ $(type -p hg) ]] || skipTest "Mercurial not installed"

clearStore

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./hg

rm -rf $repo ${repo}-tmp $TEST_HOME/.cache/nix

hg init $repo
echo '[ui]' >> $repo/.hg/hgrc
echo 'username = Foobar <foobar@example.org>' >> $repo/.hg/hgrc

# Set ui.tweakdefaults to ensure HGPLAIN is being set.
echo 'tweakdefaults = True' >> $repo/.hg/hgrc

echo utrecht > $repo/hello
touch $repo/.hgignore
hg add --cwd $repo hello .hgignore
hg commit --cwd $repo -m 'Bla1'
rev1=$(hg log --cwd $repo -r tip --template '{node}')

echo world > $repo/hello
hg commit --cwd $repo -m 'Bla2'
rev2=$(hg log --cwd $repo -r tip --template '{node}')

# Fetch an unclean branch.
echo unclean > $repo/hello
path=$(nix eval --impure --raw --expr "(builtins.fetchMercurial file://$repo).outPath")
[[ $(cat $path/hello) = unclean ]]
hg revert --cwd $repo --all

# Fetch the default branch.
path=$(nix eval --impure --raw --expr "(builtins.fetchMercurial file://$repo).outPath")
[[ $(cat $path/hello) = world ]]

# In pure eval mode, fetchGit without a revision should fail.
[[ $(nix eval --impure --raw --expr "(builtins.readFile (fetchMercurial file://$repo + \"/hello\"))") = world ]]
(! nix eval --raw --expr "builtins.readFile (fetchMercurial file://$repo + \"/hello\")")

# Fetch using an explicit revision hash.
path2=$(nix eval --impure --raw --expr "(builtins.fetchMercurial { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

# In pure eval mode, fetchGit with a revision should succeed.
[[ $(nix eval --raw --expr "builtins.readFile (fetchMercurial { url = file://$repo; rev = \"$rev2\"; } + \"/hello\")") = world ]]

# Fetch again. This should be cached.
mv $repo ${repo}-tmp
path2=$(nix eval --impure --raw --expr "(builtins.fetchMercurial file://$repo).outPath")
[[ $path = $path2 ]]

[[ $(nix eval --impure --raw --expr "(builtins.fetchMercurial file://$repo).branch") = default ]]
[[ $(nix eval --impure --expr "(builtins.fetchMercurial file://$repo).revCount") = 1 ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchMercurial file://$repo).rev") = $rev2 ]]

# But with TTL 0, it should fail.
(! nix eval --impure --refresh --expr "builtins.fetchMercurial file://$repo")

# Fetching with a explicit hash should succeed.
path2=$(nix eval --refresh --raw --expr "(builtins.fetchMercurial { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

path2=$(nix eval --refresh --raw --expr "(builtins.fetchMercurial { url = file://$repo; rev = \"$rev1\"; }).outPath")
[[ $(cat $path2/hello) = utrecht ]]

mv ${repo}-tmp $repo

# Using a clean working tree should produce the same result.
path2=$(nix eval --impure --raw --expr "(builtins.fetchMercurial $repo).outPath")
[[ $path = $path2 ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
mkdir $repo/dir1 $repo/dir2
echo foo > $repo/dir1/foo
echo bar > $repo/bar
echo bar > $repo/dir2/bar
hg add --cwd $repo dir1/foo
hg rm --cwd $repo hello

path2=$(nix eval --impure --raw --expr "(builtins.fetchMercurial $repo).outPath")
[ ! -e $path2/hello ]
[ ! -e $path2/bar ]
[ ! -e $path2/dir2/bar ]
[ ! -e $path2/.hg ]
[[ $(cat $path2/dir1/foo) = foo ]]

[[ $(nix eval --impure --raw --expr "(builtins.fetchMercurial $repo).rev") = 0000000000000000000000000000000000000000 ]]

# ... unless we're using an explicit ref.
path3=$(nix eval --impure --raw --expr "(builtins.fetchMercurial { url = $repo; rev = \"default\"; }).outPath")
[[ $path = $path3 ]]

# Committing should not affect the store path.
hg commit --cwd $repo -m 'Bla3'

path4=$(nix eval --impure --refresh --raw --expr "(builtins.fetchMercurial file://$repo).outPath")
[[ $path2 = $path4 ]]

echo paris > $repo/hello
# Passing a `name` argument should be reflected in the output path
path5=$(nix eval -vvvvv --impure --refresh --raw --expr "(builtins.fetchMercurial { url = \"file://$repo\"; name = \"foo\"; } ).outPath")
[[ $path5 =~ -foo$ ]]
