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

# Fetch a rev from another branch
git -C $repo checkout -b devtest
echo "different file" >> $TEST_ROOT/git/differentbranch
git -C $repo add differentbranch
git -C $repo commit -m 'Test2'
git -C $repo checkout master
devrev=$(git -C $repo rev-parse devtest)
out=$(nix eval --impure --raw --expr "builtins.fetchGit { url = file://$repo; rev = \"$devrev\"; }" 2>&1) || status=$?
[[ $status == 1 ]]
[[ $out =~ 'Cannot find Git revision' ]]

[[ $(nix eval --raw --expr "builtins.readFile (builtins.fetchGit { url = file://$repo; rev = \"$devrev\"; allRefs = true; } + \"/differentbranch\")") = 'different file' ]]

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
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).shortRev") = ${rev2:0:7} ]]

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
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).shortRev") = 0000000 ]]

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

# Adding a git filter does not affect the contents
#
# Background
# ==========
#
# Git filters allow the user to change how files are represented
# in the worktree.
# On checkout, the configured smudge converts blobs to files in the worktree.
# On checkin, the configured clean command converts files back into blobs.
#
# Notable uses include
#  - allow the user to work with a platform-specific representation, conveniently
#  - git-crypt: only allow some users to see file contents, transparently
#  - git-lfs: work with large files without inflating the repository
#
# See also https://git-scm.com/docs/gitattributes#_filter
#
# Why ignore filters
# ==================
#
# To quote the git docs
#
# > the intent is that if someone unsets the filter driver definition, or
# > does not have the appropriate filter program, the project should still
# > be usable.
#
# So the feature was designed to be optional. This confirms that we have a
# choice. Let's look at the individual use cases.
#
# Allow the user to work with a platform-specific representation
# --------------------------------------------------------------
#
# While this might seem convenient, any such processing can also be done in
# `postUnpack`, so it isn't necessary here.
# Tarballs from GitHub and such don't apply the smudge filter either, so if
# the project is going to be packaged in Nixpkgs, it will have to process its
# files like this anyway.
# The real kicker here is that running the smudge filter creates an
# unreproducible dependency, because the filter does not come from a pinned
# immutable source and it could inject information from arbitrary sources.
#
# Git-crypt
# ---------
#
# The nix store can be read by any process on the system, or in some cases,
# when using a cache, literally world-readable.
# Running the filters in fetchGit would essentially make impossible the use of
# git-crypt and Nix flakes in the same repository.
# Even without flakes (or with changes to the flakes feature for that matter),
# the software you want to build generally does not depend on credentials, so
# not decrypting is not only a secure default, but a good one.
# In a rare case where a build does not to decrypt a git-crypted file, one could
# still pass the decrypted file or the git-crypt key explicitly (at the cost of
# exposing it in the store, which is inevitable for nix-built paths).
#
# Git LFS
# -------
#
# Git LFS was designed to prevent excessive bloat in a repository, so the
# "smudged" versions of these files will be huge.
#
# If we were to include these directly in the `fetchGit` output, this creates
# copies of all the large files for each commit we check out, or even for
# each uncommitted but built local change (with fetchGit ./.).
#
# In many cases, those files may not even be used in the build process. If
# they are required, it seems feasible to fetch them explicitly with a
# fetcher that fetches from LFS based on the sha256 in the unsmudged files.
# It is more fine grained than downloading all LFS files and it does not even
# require IFD because it happens after fetchGit, which runs at evaluation time.
#
# If for some reason LFS support can not be achieved in Nix expressions, we
# should add support for LFS itself, without running any other filters.
#
# Conclusion
# ==========
#
# Not running the filters is more reproducible, secure and potentially more
# efficient than running them.
git -C $repo checkout master
cat >>$repo/.git/config <<EOF
# Edit quotes locally without the quotation bird tracks
[filter "quote"]
  clean = sed -e 's/^/> /'
  smudge = sed -e 's/^> //'
EOF
cat >$repo/.gitattributes <<EOF
*.q filter=quote
EOF

# Files are clean when fetching from a bare repo. This simulates a non-local repo.
echo "Insanity is building the same thing over and over and expecting different results." >$repo/einstein.q
git -C $repo add $repo/.gitattributes $repo/einstein.q
git -C $repo commit -m 'Add Einstein quote'
rev4=$(git -C $repo rev-parse HEAD)
git clone --bare $repo $repo.bare
path7=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo.bare; }).outPath")
cmp $path7/einstein.q <(echo "> Insanity is building the same thing over and over and expecting different results.")

# Files are clean when fetching from a local repo with ref.
path8=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev4\"; }).outPath")
[[ $path7 = $path8 ]]


# Files are clean when fetching from a local repo with local changes without ref or rev.

# TBD

# Explicit ref = "HEAD" should work, and produce the same outPath as without ref
path7=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; }).outPath")
path8=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; }).outPath")
[[ $path7 = $path8 ]]

# ref = "HEAD" should fetch the HEAD revision
rev4=$(git -C $repo rev-parse HEAD)
rev4_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; }).rev")
[[ $rev4 = $rev4_nix ]]
