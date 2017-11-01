source common.sh

if [[ -z $(type -p hg) ]]; then
    echo "Mercurial not installed; skipping Mercurial tests"
    exit 0
fi

clearStore

repo=$TEST_ROOT/hg

rm -rfv $repo ${repo}-tmp $TEST_HOME/.cache/nix/hg

hg init $repo
echo '[ui]' >> $repo/.hg/hgrc
echo 'username = Foobar <foobar@example.org>' >> $repo/.hg/hgrc

echo utrecht > $repo/hello
hg add --cwd $repo hello
hg commit --cwd $repo -m 'Bla1'
rev1=$(hg log --cwd $repo -r tip --template '{node}')

echo world > $repo/hello
hg commit --cwd $repo -m 'Bla2'
rev2=$(hg log --cwd $repo -r tip --template '{node}')

hg log --cwd $repo

hg log --cwd $repo -r tip --template '{node}\n'

path=$(nix eval --raw "(builtins.fetchMercurial file://$repo).outPath")
[[ $(cat $path/hello) = world ]]

# Fetch again. This should be cached.
mv $repo ${repo}-tmp
path2=$(nix eval --raw "(builtins.fetchMercurial file://$repo).outPath")
[[ $path = $path2 ]]

[[ $(nix eval --raw "(builtins.fetchMercurial file://$repo).branch") = default ]]
[[ $(nix eval "(builtins.fetchMercurial file://$repo).revCount") = 1 ]]
[[ $(nix eval --raw "(builtins.fetchMercurial file://$repo).rev") = $rev2 ]]

# But with TTL 0, it should fail.
(! nix eval --tarball-ttl 0 --raw "(builtins.fetchMercurial file://$repo)")

mv ${repo}-tmp $repo

# Using a clean working tree should produce the same result.
path2=$(nix eval --raw "(builtins.fetchMercurial $repo).outPath")
[[ $path = $path2 ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
echo foo > $repo/foo
echo bar > $repo/bar
hg add --cwd $repo foo
hg rm --cwd $repo hello

path2=$(nix eval --raw "(builtins.fetchMercurial $repo).outPath")
[ ! -e $path2/hello ]
[ ! -e $path2/bar ]
[[ $(cat $path2/foo) = foo ]]

[[ $(nix eval --raw "(builtins.fetchMercurial $repo).rev") = 0000000000000000000000000000000000000000 ]]

# ... unless we're using an explicit rev.
path3=$(nix eval --raw "(builtins.fetchMercurial { url = $repo; rev = \"default\"; }).outPath")
[[ $path = $path3 ]]

# Committing should not affect the store path.
hg commit --cwd $repo -m 'Bla3'

path4=$(nix eval --tarball-ttl 0 --raw "(builtins.fetchMercurial file://$repo).outPath")
[[ $path2 = $path4 ]]
