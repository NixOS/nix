source common.sh

if [[ -z $(type -p hg) ]]; then
    echo "Mercurial not installed; skipping Mercurial tests"
    exit 99
fi

clearStore

repo=$TEST_ROOT/hg

rm -rf $repo ${repo}-tmp $TEST_HOME/.cache/nix/hg

hg init $repo
echo '[ui]' >> $repo/.hg/hgrc
echo 'username = Foobar <foobar@example.org>' >> $repo/.hg/hgrc

echo utrecht > $repo/hello
touch $repo/.hgignore
hg add --cwd $repo hello .hgignore
hg commit --cwd $repo -m 'Bla1'
rev1=$(hg log --cwd $repo -r tip --template '{node}')

echo world > $repo/hello
hg commit --cwd $repo -m 'Bla2'
rev2=$(hg log --cwd $repo -r tip --template '{node}')

# Fetch the default branch.
path=$(nix eval --raw "(builtins.fetchMercurial file://$repo).outPath")
[[ $(cat $path/hello) = world ]]

# In pure eval mode, fetchGit without a revision should fail.
[[ $(nix eval --raw "(builtins.readFile (fetchMercurial file://$repo + \"/hello\"))") = world ]]
(! nix eval --pure-eval --raw "(builtins.readFile (fetchMercurial file://$repo + \"/hello\"))")

# Fetch using an explicit revision hash.
path2=$(nix eval --raw "(builtins.fetchMercurial { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

# In pure eval mode, fetchGit with a revision should succeed.
[[ $(nix eval --pure-eval --raw "(builtins.readFile (fetchMercurial { url = file://$repo; rev = \"$rev2\"; } + \"/hello\"))") = world ]]

# Fetch again. This should be cached.
mv $repo ${repo}-tmp
path2=$(nix eval --raw "(builtins.fetchMercurial file://$repo).outPath")
[[ $path = $path2 ]]

[[ $(nix eval --raw "(builtins.fetchMercurial file://$repo).branch") = default ]]
[[ $(nix eval "(builtins.fetchMercurial file://$repo).revCount") = 1 ]]
[[ $(nix eval --raw "(builtins.fetchMercurial file://$repo).rev") = $rev2 ]]

# But with TTL 0, it should fail.
(! nix eval --tarball-ttl 0 "(builtins.fetchMercurial file://$repo)")

# Fetching with a explicit hash should succeed.
path2=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchMercurial { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = $path2 ]]

path2=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchMercurial { url = file://$repo; rev = \"$rev1\"; }).outPath")
[[ $(cat $path2/hello) = utrecht ]]

mv ${repo}-tmp $repo

# Using a clean working tree should produce the same result.
path2=$(nix eval --raw "(builtins.fetchMercurial $repo).outPath")
[[ $path = $path2 ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
mkdir $repo/dir1 $repo/dir2
echo foo > $repo/dir1/foo
echo bar > $repo/bar
echo bar > $repo/dir2/bar
hg add --cwd $repo dir1/foo
hg rm --cwd $repo hello

path2=$(nix eval --raw "(builtins.fetchMercurial $repo).outPath")
[ ! -e $path2/hello ]
[ ! -e $path2/bar ]
[ ! -e $path2/dir2/bar ]
[ ! -e $path2/.hg ]
[[ $(cat $path2/dir1/foo) = foo ]]

[[ $(nix eval --raw "(builtins.fetchMercurial $repo).rev") = 0000000000000000000000000000000000000000 ]]

# ... unless we're using an explicit rev.
path3=$(nix eval --raw "(builtins.fetchMercurial { url = $repo; rev = \"default\"; }).outPath")
[[ $path = $path3 ]]

# Committing should not affect the store path.
hg commit --cwd $repo -m 'Bla3'

path4=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchMercurial file://$repo).outPath")
[[ $path2 = $path4 ]]
