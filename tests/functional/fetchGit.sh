#!/usr/bin/env bash

source common.sh

requireGit

clearStoreIfPossible

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./git

export _NIX_FORCE_HTTP=1

rm -rf "$repo" "${repo}"-tmp "$TEST_HOME"/.cache/nix "$TEST_ROOT"/worktree "$TEST_ROOT"/minimal

git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"

echo utrecht > "$repo"/hello
touch "$repo"/.gitignore
git -C "$repo" add hello .gitignore
git -C "$repo" commit -m 'Bla1'
rev1=$(git -C "$repo" rev-parse HEAD)
git -C "$repo" tag -a tag1 -m tag1

echo world > "$repo"/hello
git -C "$repo" commit -m 'Bla2' -a
git -C "$repo" worktree add "$TEST_ROOT"/worktree
echo hello >> "$TEST_ROOT"/worktree/hello
rev2=$(git -C "$repo" rev-parse HEAD)
git -C "$repo" tag -a tag2 -m tag2

# Check whether fetching in read-only mode works.
nix-instantiate --eval -E "builtins.readFile ((builtins.fetchGit file://$TEST_ROOT/worktree) + \"/hello\") == \"utrecht\\n\""

# Fetch a worktree.
unset _NIX_FORCE_HTTP
expectStderr 0 nix eval -vvvv --impure --raw --expr "(builtins.fetchGit file://$TEST_ROOT/worktree).outPath" | grepQuiet "copying '$TEST_ROOT/worktree/' to the store"
path0=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$TEST_ROOT/worktree).outPath")
path0_=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$TEST_ROOT/worktree; }).outPath")
[[ $path0 = "$path0_" ]]
path0_=$(nix eval --impure --raw --expr "(builtins.fetchTree git+file://$TEST_ROOT/worktree).outPath")
[[ $path0 = "$path0_" ]]
export _NIX_FORCE_HTTP=1
[[ $(tail -n 1 "$path0"/hello) = "hello" ]]

# Nuke the cache
rm -rf "$TEST_HOME"/.cache/nix

# Fetch the default branch.
path=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")
[[ $(cat "$path"/hello) = world ]]

# Fetch again. This should be cached.
# NOTE: This has to be done before the test case below which tries to pack-refs
# the reason being that the lookup on the cache uses the ref-file `/refs/heads/master`
# which does not exist after packing.
mv "$repo" "${repo}"-tmp
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")
[[ $path = "$path2" ]]

[[ $(nix eval --impure --expr "(builtins.fetchGit file://$repo).revCount") = 2 ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).rev") = "$rev2" ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).shortRev") = "${rev2:0:7}" ]]

# Fetching with a explicit hash should succeed.
path2=$(nix eval --refresh --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = "$path2" ]]

path2=$(nix eval --refresh --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev1\"; }).outPath")
[[ $(cat "$path2"/hello) = utrecht ]]

mv "${repo}"-tmp "$repo"

# Fetch when the cache has packed-refs
# Regression test of #8822
git -C "$TEST_HOME"/.cache/nix/gitv3/*/ pack-refs --all
path=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")

# Fetch a rev from another branch
git -C "$repo" checkout -b devtest
echo "different file" >> "$TEST_ROOT"/git/differentbranch
git -C "$repo" add differentbranch
git -C "$repo" commit -m 'Test2'
git -C "$repo" checkout master
devrev=$(git -C "$repo" rev-parse devtest)
nix eval --raw --expr "builtins.fetchGit { url = file://$repo; rev = \"$devrev\"; }"

[[ $(nix eval --raw --expr "builtins.readFile (builtins.fetchGit { url = file://$repo; rev = \"$devrev\"; allRefs = true; } + \"/differentbranch\")") = 'different file' ]]

# In pure eval mode, fetchGit without a revision should fail.
[[ $(nix eval --impure --raw --expr "builtins.readFile (fetchGit file://$repo + \"/hello\")") = world ]]
(! nix eval --raw --expr "builtins.readFile (fetchGit file://$repo + \"/hello\")")

# Fetch using an explicit revision hash.
path2=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev2\"; }).outPath")
[[ $path = "$path2" ]]

# In pure eval mode, fetchGit with a revision should succeed.
[[ $(nix eval --raw --expr "builtins.readFile (fetchGit { url = file://$repo; rev = \"$rev2\"; } + \"/hello\")") = world ]]

# But without a hash, it fails.
expectStderr 1 nix eval --expr 'builtins.fetchGit "file:///foo"' | grepQuiet "'fetchGit' doesn't fetch unlocked input"

# Using a clean working tree should produce the same result.
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $path = "$path2" ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
mkdir "$repo"/dir1 "$repo"/dir2
echo foo > "$repo"/dir1/foo
echo bar > "$repo"/bar
echo bar > "$repo"/dir2/bar
git -C "$repo" add dir1/foo
git -C "$repo" rm hello

unset _NIX_FORCE_HTTP
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[ ! -e "$path2"/hello ]
[ ! -e "$path2"/bar ]
[ ! -e "$path2"/dir2/bar ]
[ ! -e "$path2"/.git ]
[[ $(cat "$path2"/dir1/foo) = foo ]]

[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).dirtyRev") = "${rev2}-dirty" ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).dirtyShortRev") = "${rev2:0:7}-dirty" ]]

# ... unless we're using an explicit ref or rev.
path3=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"master\"; }).outPath")
[[ $path = "$path3" ]]

path3=$(nix eval --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; }).outPath")
[[ $path = "$path3" ]]

# Committing should not affect the store path.
git -C "$repo" commit -m 'Bla3' -a

path4=$(nix eval --impure --refresh --raw --expr "(builtins.fetchGit file://$repo).outPath")
[[ $path2 = "$path4" ]]

[[ $(nix eval --impure --expr "builtins.hasAttr \"rev\" (builtins.fetchGit $repo)") == "true" ]]
[[ $(nix eval --impure --expr "builtins.hasAttr \"dirtyRev\" (builtins.fetchGit $repo)") == "false" ]]
[[ $(nix eval --impure --expr "builtins.hasAttr \"dirtyShortRev\" (builtins.fetchGit $repo)") == "false" ]]

expect 102 nix eval --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; narHash = \"sha256-B5yIPHhEm0eysJKEsO7nqxprh9vcblFxpJG11gXJus1=\"; }).outPath"

path5=$(nix eval --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; narHash = \"sha256-Hr8g6AqANb3xqX28eu1XnjK/3ab8Gv6TJSnkb1LezG9=\"; }).outPath")
[[ $path = "$path5" ]]

# Ensure that NAR hashes are checked.
expectStderr 102 nix eval --raw --expr "(builtins.fetchGit { url = $repo; rev = \"$rev2\"; narHash = \"sha256-Hr8g6AqANb4xqX28eu1XnjK/3ab8Gv6TJSnkb1LezG9=\"; }).outPath" | grepQuiet "error: NAR hash mismatch"

# It's allowed to use only a narHash, but you should get a warning.
expectStderr 0 nix eval --raw --expr "(builtins.fetchGit { url = $repo; ref = \"tag2\"; narHash = \"sha256-Hr8g6AqANb3xqX28eu1XnjK/3ab8Gv6TJSnkb1LezG9=\"; }).outPath" | grepQuiet "warning: Input .* is unlocked"

# tarball-ttl should be ignored if we specify a rev
echo delft > "$repo"/hello
git -C "$repo" add hello
git -C "$repo" commit -m 'Bla4'
rev3=$(git -C "$repo" rev-parse HEAD)
nix eval --tarball-ttl 3600 --expr "builtins.fetchGit { url = $repo; rev = \"$rev3\"; }" >/dev/null

# Update 'path' to reflect latest master
path=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")

# Check behavior when non-master branch is used
git -C "$repo" checkout "$rev2" -b dev
echo dev > "$repo"/hello

# File URI uses dirty tree unless specified otherwise
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit file://$repo).outPath")
[ "$(cat "$path2"/hello)" = dev ]

# Using local path with branch other than 'master' should work when clean or dirty
path3=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
# (check dirty-tree handling was used)
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).shortRev") = 0000000 ]]
# Making a dirty tree clean again and fetching it should
# record correct revision information. See: #4140
echo world > "$repo"/hello
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = "$rev2" ]]

# Committing shouldn't change store path, or switch to using 'master'
echo dev > "$repo"/hello
git -C "$repo" commit -m 'Bla5' -a
path4=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $(cat "$path4"/hello) = dev ]]
[[ $path3 = "$path4" ]]

# Using remote path with branch other than 'master' should fetch the HEAD revision.
# (--tarball-ttl 0 to prevent using the cached repo above)
export _NIX_FORCE_HTTP=1
path4=$(nix eval --tarball-ttl 0 --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $(cat "$path4"/hello) = dev ]]
[[ $path3 = "$path4" ]]
unset _NIX_FORCE_HTTP

# Confirm same as 'dev' branch
path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = "$path5" ]]


# Nuke the cache
rm -rf "$TEST_HOME"/.cache/nix

# Try again. This should work.
path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = "$path5" ]]

# Fetching from a repo with only a specific revision and no branches should
# not fall back to copying files and record correct revision information. See: #5302
mkdir "$TEST_ROOT"/minimal
git -C "$TEST_ROOT"/minimal init
git -C "$TEST_ROOT"/minimal fetch "$repo" "$rev2"
git -C "$TEST_ROOT"/minimal checkout "$rev2"
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit { url = $TEST_ROOT/minimal; }).rev") = "$rev2" ]]

# Explicit ref = "HEAD" should work, and produce the same outPath as without ref
path7=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; }).outPath")
path8=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; }).outPath")
[[ $path7 = "$path8" ]]

# ref = "HEAD" should fetch the HEAD revision
rev4=$(git -C "$repo" rev-parse HEAD)
rev4_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; }).rev")
[[ $rev4 = "$rev4_nix" ]]

# The name argument should be handled
path9=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; name = \"foo\"; }).outPath")
[[ $path9 =~ -foo$ ]]

# Specifying a ref without a rev shouldn't pick a cached rev for a different ref
export _NIX_FORCE_HTTP=1
rev_tag1_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"refs/tags/tag1\"; }).rev")
# shellcheck disable=SC1083
rev_tag1=$(git -C "$repo" rev-parse refs/tags/tag1^{commit})
[[ $rev_tag1_nix = "$rev_tag1" ]]
rev_tag2_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"refs/tags/tag2\"; }).rev")
# shellcheck disable=SC1083
rev_tag2=$(git -C "$repo" rev-parse refs/tags/tag2^{commit})
[[ $rev_tag2_nix = "$rev_tag2" ]]
unset _NIX_FORCE_HTTP

# Ensure .gitattributes is respected
touch "$repo"/not-exported-file
touch "$repo"/exported-wonky
echo "/not-exported-file export-ignore" >> "$repo"/.gitattributes
echo "/exported-wonky export-ignore=wonk" >> "$repo"/.gitattributes
git -C "$repo" add not-exported-file exported-wonky .gitattributes
git -C "$repo" commit -m 'Bla6'
rev5=$(git -C "$repo" rev-parse HEAD)
path12=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$repo; rev = \"$rev5\"; }).outPath")
[[ ! -e $path12/not-exported-file ]]
[[ -e $path12/exported-wonky ]]

# should fail if there is no repo
rm -rf "$repo"/.git
rm -rf "$TEST_HOME"/.cache/nix
(! nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")

# should succeed for a repo without commits
git init "$repo"
git -C "$repo" add hello # need to add at least one file to cause the root of the repo to be visible
# shellcheck disable=SC2034
path10=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")

# should succeed for a path with a space
# regression test for #7707
repo="$TEST_ROOT/a b"
git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"

echo utrecht > "$repo/hello"
touch "$repo/.gitignore"
git -C "$repo" add hello .gitignore
git -C "$repo" commit -m 'Bla1'
cd "$repo"
# shellcheck disable=SC2034
path11=$(nix eval --impure --raw --expr "(builtins.fetchGit ./.).outPath")

# Test a workdir with no commits.
empty="$TEST_ROOT/empty"
git init "$empty"

emptyAttrs="{ lastModified = 0; lastModifiedDate = \"19700101000000\"; narHash = \"sha256-pQpattmS9VmO3ZIQUFn66az8GSmB4IvYhTTCFn6SUmo=\"; rev = \"0000000000000000000000000000000000000000\"; revCount = 0; shortRev = \"0000000\"; submodules = false; }"
result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchGit $empty) [\"outPath\"]")
[[ "$result" = "$emptyAttrs" ]]

echo foo > "$empty/x"

result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchGit $empty) [\"outPath\"]")
[[ "$result" = "$emptyAttrs" ]]

git -C "$empty" add x

expected_attrs="{ lastModified = 0; lastModifiedDate = \"19700101000000\"; narHash = \"sha256-wzlAGjxKxpaWdqVhlq55q5Gxo4Bf860+kLeEa/v02As=\"; rev = \"0000000000000000000000000000000000000000\"; revCount = 0; shortRev = \"0000000\"; submodules = false; }"
result=$(nix eval --impure --expr "builtins.removeAttrs (builtins.fetchGit $empty) [\"outPath\"]")
[[ "$result" = "$expected_attrs" ]]

# Test a repo with an empty commit.
git -C "$empty" rm -f x

git -C "$empty" config user.email "foobar@example.com"
git -C "$empty" config user.name "Foobar"
git -C "$empty" commit --allow-empty --allow-empty-message --message ""

nix eval --impure --expr "let attrs = builtins.fetchGit $empty; in assert attrs.lastModified != 0; assert attrs.rev != \"0000000000000000000000000000000000000000\"; assert attrs.revCount == 1; true"

# Test a repo with `eol=crlf`.
repo="$TEST_ROOT/crlf"
git init "$repo"
git -C $repo config user.email "foobar@example.com"
git -C $repo config user.name "Foobar"

echo -n -e 'foo\nbar\nbaz' > "$repo/newlines.txt"
echo -n -e 'foo\nbar\nbaz' > "$repo/test.txt"
git -C "$repo" add newlines.txt test.txt
git -C "$repo" commit -m 'Add files containing LF line endings'
echo 'test.txt eol=crlf' > "$repo/.gitattributes"
git -C "$repo" add .gitattributes
git -C "$repo" commit -m 'Add eol=crlf to gitattributes'
narhash=$(nix eval --raw --impure --expr "(builtins.fetchGit { url = \"$repo\"; ref = \"master\"; }).narHash")
[[ "$narhash" = "sha256-BBhuj+vOnwCUnk5az22PwAnF32KE1aulWAVfCQlbW7U=" ]]

narhash=$(nix eval --raw --impure --expr "(builtins.fetchGit { url = \"$repo\"; ref = \"master\"; applyFilters = true; }).narHash")
[[ "$narhash" = "sha256-k7u7RAaF+OvrbtT3KCCDQA8e9uOdflUo5zSgsosoLzA=" ]]


# Ensure that NAR hash doesn't depend on user configuration.
rm -rf $TEST_HOME/.cache/nix
export GIT_CONFIG_GLOBAL="$TEST_ROOT/gitconfig"
git config --global core.autocrlf true
narhash=$(nix eval --raw --impure --expr "(builtins.fetchGit { url = \"$repo\"; ref = \"master\"; }).narHash")
[[ "$narhash" = "sha256-BBhuj+vOnwCUnk5az22PwAnF32KE1aulWAVfCQlbW7U=" ]]
narhash=$(nix eval --raw --impure --expr "(builtins.fetchGit { url = \"$repo\"; ref = \"master\"; applyFilters = true; }).narHash")
[[ "$narhash" = "sha256-k7u7RAaF+OvrbtT3KCCDQA8e9uOdflUo5zSgsosoLzA=" ]]
unset GIT_CONFIG_GLOBAL
