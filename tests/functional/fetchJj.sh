#!/usr/bin/env bash

source common.sh

[[ $(type -p jj) ]] || skipTest "Jujutsu not installed"

TODO_NixOS

clearStoreIfPossible

# Give jj a deterministic identity and keep it from reading the user's config.
export JJ_CONFIG=$TEST_ROOT/jjconfig.toml
cat > "$JJ_CONFIG" <<EOF
[user]
name = "Nix Test"
email = "test@example.org"
EOF

repo=$TEST_ROOT/jj

jj git init "$repo" >/dev/null

echo utrecht > "$repo"/hello
mkdir "$repo"/dir
echo world > "$repo"/dir/foo

# Untracked / ignored files that must NOT end up in the store.
cat > "$repo"/.gitignore <<EOF
result
*.tmp
build/
EOF
echo junk > "$repo"/scratch.tmp
mkdir "$repo"/build
echo artifact > "$repo"/build/out

# $1: extra fields to splice into the fetchTree argument set (e.g. '; name = "foo"').
# $2: attribute to read from the result. `toString` makes this work for both
#     string attrs (outPath, rev, ref) and integer attrs (revCount, lastModified).
fetchjj() {
    nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
        "toString (builtins.fetchTree { type = \"jj\"; url = \"file://$repo\"$1; }).$2"
}

# Basic fetch of the working copy. Only tracked files should be present.
path=$(fetchjj "" outPath)
[[ $(cat "$path"/hello) = utrecht ]]
[[ $(cat "$path"/dir/foo) = world ]]
[[ -e "$path"/.gitignore ]]
[[ ! -e "$path"/scratch.tmp ]]
[[ ! -e "$path"/build ]]
[[ ! -e "$path"/.jj ]]
[[ ! -e "$path"/.git ]]

# The working copy is always identified by a revision (jj has no "dirty" state).
rev=$(fetchjj "" rev)
[[ $rev =~ ^[0-9a-f]{40}$ ]]

# revCount and lastModified are exposed.
[[ $(fetchjj "" revCount) -ge 1 ]]
[[ $(fetchjj "" lastModified) -gt 0 ]]

# Fetching again without changes is cached and yields the same path.
path2=$(fetchjj "" outPath)
[[ $path = "$path2" ]]

# Editing a tracked file changes the revision and the store path (no commit needed).
echo amsterdam > "$repo"/hello
rev2=$(fetchjj "" rev)
[[ $rev != "$rev2" ]]
path3=$(fetchjj "" outPath)
[[ $path != "$path3" ]]
[[ $(cat "$path3"/hello) = amsterdam ]]

# Adding a new file makes it tracked and visible (jj auto-tracks on snapshot).
echo new > "$repo"/dir/bar
path4=$(fetchjj "" outPath)
[[ $(cat "$path4"/dir/bar) = new ]]

# Filenames with special characters, including spaces and embedded newlines, are
# tracked and copied correctly (the file list is parsed NUL-separated).
echo spaced > "$repo/a file with spaces"
weird=$(printf 'a\nb')   # a filename containing a newline
echo nl > "$repo/$weird"
path=$(fetchjj "" outPath)
[[ $(cat "$path/a file with spaces") = spaced ]]
[[ $(cat "$path/$weird") = nl ]]

# Add an executable and a symlink, then fetch an explicit revision. The tree is
# reconstructed via the jj CLI; asserting that it yields the *same* store path as
# the working copy it was taken from verifies byte-for-byte fidelity (content,
# executable bit, symlinks and all).
chmod +x "$repo"/dir/bar
ln -s hello "$repo"/symlink
# A symlink whose target begins with '+' exercises the git-diff parser used to
# recover symlink targets (the target must not be mistaken for a diff marker).
ln -s '++/odd/target' "$repo"/pluslink
workdirPath=$(fetchjj "" outPath)
rev=$(fetchjj "" rev)
revPath=$(nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "toString (builtins.fetchTree { type = \"jj\"; url = \"file://$repo\"; rev = \"$rev\"; }).outPath")
[[ $workdirPath = "$revPath" ]]
[[ $(cat "$revPath"/hello) = amsterdam ]]
[[ -x "$revPath"/dir/bar ]]
[[ -L "$revPath"/symlink && $(readlink "$revPath"/symlink) = hello ]]
[[ -L "$revPath"/pluslink && $(readlink "$revPath"/pluslink) = ++/odd/target ]]

# A jj input with an explicit revision is locked.
[[ $(nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "(builtins.fetchTree { type = \"jj\"; url = \"file://$repo\"; rev = \"$rev\"; }).rev") = "$rev" ]]

# A bookmark can be fetched via `ref` and resolves to the same revision.
jj --repository "$repo" bookmark create release -r @ >/dev/null
refPath=$(nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "toString (builtins.fetchTree { type = \"jj\"; url = \"file://$repo\"; ref = \"release\"; }).outPath")
[[ $revPath = "$refPath" ]]

# A flake in a Jujutsu workspace (which has a .jj but no .git) must be routed to
# the jj fetcher rather than the unfiltered path fetcher. This is the case the
# feature was added for.
ws=$TEST_ROOT/jj-workspace
jj --repository "$repo" workspace add "$ws" >/dev/null
cat > "$ws"/flake.nix <<'EOF'
{
  outputs = { self, ... }: {
    answer = 42;
    hasFlake = builtins.pathExists (self + "/flake.nix");
    hasScratch = builtins.pathExists (self + "/scratch.tmp");
  };
}
EOF
printf '*.tmp\n' > "$ws"/.gitignore
echo junk > "$ws"/scratch.tmp

# The flake resolves and evaluates.
[[ $(nix eval "$ws"#answer) = 42 ]]

# It was routed to the jj fetcher.
nix flake metadata "$ws" | grepQuiet "jj+file"

# And its source is filtered to tracked files only.
[[ $(nix eval "$ws"#hasFlake) = true ]]
[[ $(nix eval "$ws"#hasScratch) = false ]]

# A colocated repository (`jj git init --colocate`) has both `.git` and `.jj`.
# It is routed to the jj fetcher (the `.jj` directory takes precedence), since
# its presence is a deliberate choice to use jj.
colocated=$TEST_ROOT/jj-colocated
jj git init --colocate "$colocated" >/dev/null
[[ -d $colocated/.git && -d $colocated/.jj ]]
echo '{ outputs = _: { }; }' > "$colocated"/flake.nix
nix flake metadata "$colocated" | grepQuiet "jj+file"

# A fresh, empty repository (the '@' commit has no files) fetches to an empty
# tree without error, and still exposes a valid revision.
empty=$TEST_ROOT/jj-empty
jj git init "$empty" >/dev/null
emptyPath=$(nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "toString (builtins.fetchTree { type = \"jj\"; url = \"file://$empty\"; }).outPath")
[[ -d $emptyPath ]]
[[ -z $(ls -A "$emptyPath") ]]
nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "(builtins.fetchTree { type = \"jj\"; url = \"file://$empty\"; }).rev" | grepQuiet -E '^[0-9a-f]{40}$'

# ---------------------------------------------------------------------------
# Remote repositories: jj's only remote backend is Git, so jj+git+<transport>
# clones a Git repository via `jj git clone`. We exercise this hermetically with
# a local Git repo served over file://, and cross-check against the Git fetcher.
# ---------------------------------------------------------------------------
[[ $(type -p git) ]] || skipTest "Git not installed"

gitRemote=$TEST_ROOT/git-remote
git -c init.defaultBranch=main init -q "$gitRemote"
git -C "$gitRemote" config user.name "Nix Test"
git -C "$gitRemote" config user.email test@example.org
git -C "$gitRemote" config commit.gpgsign false
echo one > "$gitRemote"/file1
git -C "$gitRemote" add file1
git -C "$gitRemote" commit -q -m c1
echo two > "$gitRemote"/file2
git -C "$gitRemote" add file2
git -C "$gitRemote" commit -q -m c2
gitRev=$(git -C "$gitRemote" rev-parse HEAD)

fetchjjremote() {
    # $1: extra fields; $2: attribute to read.
    nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
        "toString (builtins.fetchTree { type = \"jj\"; url = \"git+file://$gitRemote\"$1; }).$2"
}

# Full clone: tracked files present, no VCS metadata, and the resolved rev and
# revCount match the Git repository exactly.
remotePath=$(fetchjjremote "" outPath)
[[ $(cat "$remotePath"/file1) = one ]]
[[ $(cat "$remotePath"/file2) = two ]]
[[ ! -e "$remotePath"/.jj && ! -e "$remotePath"/.git ]]
[[ $(fetchjjremote "" rev) = "$gitRev" ]]
[[ $(nix eval --extra-experimental-features fetch-tree --impure --expr \
    "(builtins.fetchTree { type = \"jj\"; url = \"git+file://$gitRemote\"; }).revCount") = 2 ]]

# The flake-style URL form resolves identically.
[[ $(nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "(builtins.fetchTree \"jj+git+file://$gitRemote\").rev") = "$gitRev" ]]

# The materialised tree is byte-for-byte what the Git fetcher produces.
gitPath=$(nix eval --extra-experimental-features fetch-tree --impure --raw --expr \
    "toString (builtins.fetchTree { type = \"git\"; url = \"file://$gitRemote\"; rev = \"$gitRev\"; }).outPath")
diff -r "$remotePath" "$gitPath"

# Fetching an explicit revision is locked and reproducible.
[[ $(fetchjjremote "; rev = \"$gitRev\"" rev) = "$gitRev" ]]

# A shallow clone produces the same tree, but (like the Git fetcher) exposes no
# revCount, since the truncated history can't be counted.
shallowPath=$(fetchjjremote "; shallow = true" outPath)
diff -r "$shallowPath" "$gitPath"
expectStderr 1 nix eval --extra-experimental-features fetch-tree --impure --expr \
    "(builtins.fetchTree { type = \"jj\"; url = \"git+file://$gitRemote\"; shallow = true; }).revCount" \
    | grepQuiet "revCount"
