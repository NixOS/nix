#!/usr/bin/env bash

source common.sh

set -u

requireGit

clearStoreIfPossible

rootRepo=$TEST_ROOT/gitSubmodulesRoot
subRepo=$TEST_ROOT/gitSubmodulesSub

rm -rf "${rootRepo}" "${subRepo}" "$TEST_HOME"/.cache/nix

# Submodules can't be fetched locally by default, which can cause
# information leakage vulnerabilities, but for these tests our
# submodule is intentionally local and it's all trusted, so we
# disable this restriction. Setting it per repo is not sufficient, as
# the repo-local config does not apply to the commands run from
# outside the repos by Nix.
export XDG_CONFIG_HOME=$TEST_HOME/.config
git config --global protocol.file.allow always

initGitRepo() {
    git init "$1"
    git -C "$1" config user.email "foobar@example.com"
    git -C "$1" config user.name "Foobar"
}

addGitContent() {
    echo "lorem ipsum" > "$1"/content
    git -C "$1" add content
    git -C "$1" commit -m "Initial commit"
}

initGitRepo "$subRepo"
addGitContent "$subRepo"

initGitRepo "$rootRepo"

git -C "$rootRepo" submodule init
git -C "$rootRepo" submodule add "$subRepo" sub
git -C "$rootRepo" add sub
git -C "$rootRepo" commit -m "Add submodule"

rev=$(git -C "$rootRepo" rev-parse HEAD)

r1=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; }).outPath")
r2=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = false; }).outPath")
r3=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = true; }).outPath")

[[ $r1 == "$r2" ]]
[[ $r2 != "$r3" ]]

r4=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; }).outPath")
r5=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = false; }).outPath")
r6=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = true; }).outPath")
r7=$(nix eval --raw --expr "(builtins.fetchGit { url = $rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = true; }).outPath")
r8=$(nix eval --raw --expr "(builtins.fetchGit { url = $rootRepo; rev = \"$rev\"; submodules = true; }).outPath")

[[ $r1 == "$r4" ]]
[[ $r4 == "$r5" ]]
[[ $r3 == "$r6" ]]
[[ $r6 == "$r7" ]]
[[ $r7 == "$r8" ]]

have_submodules=$(nix eval --expr "(builtins.fetchGit { url = $rootRepo; rev = \"$rev\"; }).submodules")
[[ $have_submodules == false ]]

have_submodules=$(nix eval --expr "(builtins.fetchGit { url = $rootRepo; rev = \"$rev\"; submodules = false; }).submodules")
[[ $have_submodules == false ]]

have_submodules=$(nix eval --expr "(builtins.fetchGit { url = $rootRepo; rev = \"$rev\"; submodules = true; }).submodules")
[[ $have_submodules == true ]]

pathWithoutSubmodules=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; }).outPath")
pathWithSubmodules=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = true; }).outPath")
pathWithSubmodulesAgain=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = true; }).outPath")
pathWithSubmodulesAgainWithRef=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = true; }).outPath")

# The resulting store path cannot be the same.
[[ $pathWithoutSubmodules != "$pathWithSubmodules" ]]

# Checking out the same repo with submodules returns in the same store path.
[[ $pathWithSubmodules == "$pathWithSubmodulesAgain" ]]

# Checking out the same repo with submodules returns in the same store path.
[[ $pathWithSubmodulesAgain == "$pathWithSubmodulesAgainWithRef" ]]

# The submodules flag is actually honored.
[[ ! -e $pathWithoutSubmodules/sub/content ]]
[[ -e $pathWithSubmodules/sub/content ]]

[[ -e $pathWithSubmodulesAgainWithRef/sub/content ]]

# No .git directory or submodule reference files must be left
test "$(find "$pathWithSubmodules" -name .git)" = ""

# Git repos without submodules can be fetched with submodules = true.
subRev=$(git -C "$subRepo" rev-parse HEAD)
noSubmoduleRepoBaseline=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$subRepo; rev = \"$subRev\"; }).outPath")
noSubmoduleRepo=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$subRepo; rev = \"$subRev\"; submodules = true; }).outPath")

[[ $noSubmoduleRepoBaseline == "$noSubmoduleRepo" ]]

# Test .gitmodules with entries that refer to non-existent objects or objects that are not submodules.
cat >> "$rootRepo"/.gitmodules <<EOF
[submodule "missing"]
        path = missing
        url = https://example.org/missing.git

[submodule "file"]
        path = file
        url = https://example.org/file.git
EOF
echo foo > "$rootRepo"/file
git -C "$rootRepo" add file
git -C "$rootRepo" commit -a -m "Add bad submodules"

rev=$(git -C "$rootRepo" rev-parse HEAD)

r=$(nix eval --raw --expr "builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = true; }")

[[ -f $r/file ]]
[[ ! -e $r/missing ]]

# Test relative submodule URLs.
rm "$TEST_HOME"/.cache/nix/fetcher-cache*
rm -rf "$rootRepo"/.git "$rootRepo"/.gitmodules "$rootRepo"/sub
initGitRepo "$rootRepo"
git -C "$rootRepo" submodule add ../gitSubmodulesSub sub
git -C "$rootRepo" commit -m "Add submodule"
rev2=$(git -C "$rootRepo" rev-parse HEAD)
pathWithRelative=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev2\"; submodules = true; }).outPath")
diff -r -x .gitmodules "$pathWithSubmodules" "$pathWithRelative"

# Test clones that have an upstream with relative submodule URLs.
rm "$TEST_HOME"/.cache/nix/fetcher-cache*
cloneRepo=$TEST_ROOT/a/b/gitSubmodulesClone # NB /a/b to make the relative path not work relative to $cloneRepo
git clone "$rootRepo" "$cloneRepo"
pathIndirect=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$cloneRepo; rev = \"$rev2\"; submodules = true; }).outPath")
[[ $pathIndirect = "$pathWithRelative" ]]

# Test submodule export-ignore interaction
git -C "$rootRepo"/sub config user.email "foobar@example.com"
git -C "$rootRepo"/sub config user.name "Foobar"

echo "/exclude-from-root export-ignore" >> "$rootRepo"/.gitattributes
# TBD possible semantics for submodules + exportIgnore
# echo "/sub/exclude-deep export-ignore" >> $rootRepo/.gitattributes
echo nope > "$rootRepo"/exclude-from-root
git -C "$rootRepo" add .gitattributes exclude-from-root
git -C "$rootRepo" commit -m "Add export-ignore"

echo "/exclude-from-sub export-ignore" >> "$rootRepo"/sub/.gitattributes
echo nope > "$rootRepo"/sub/exclude-from-sub
# TBD possible semantics for submodules + exportIgnore
# echo aye > $rootRepo/sub/exclude-from-root
git -C "$rootRepo"/sub add .gitattributes exclude-from-sub
git -C "$rootRepo"/sub commit -m "Add export-ignore (sub)"

git -C "$rootRepo" add sub
git -C "$rootRepo" commit -m "Update submodule"

git -C "$rootRepo" status

# # TBD: not supported yet, because semantics are undecided and current implementation leaks rules from the root to submodules
# # exportIgnore can be used with submodules
# pathWithExportIgnore=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$rootRepo; submodules = true; exportIgnore = true; }).outPath")
# # find $pathWithExportIgnore
# # git -C $rootRepo archive --format=tar HEAD | tar -t
# # cp -a $rootRepo /tmp/rootRepo

# [[ -e $pathWithExportIgnore/sub/content ]]
# [[ ! -e $pathWithExportIgnore/exclude-from-root ]]
# [[ ! -e $pathWithExportIgnore/sub/exclude-from-sub ]]
# TBD possible semantics for submodules + exportIgnore
# # root .gitattribute has no power across submodule boundary
# [[ -e $pathWithExportIgnore/sub/exclude-from-root ]]
# [[ -e $pathWithExportIgnore/sub/exclude-deep ]]


# exportIgnore can be explicitly disabled with submodules
pathWithoutExportIgnore=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$rootRepo; submodules = true; exportIgnore = false; }).outPath")
# find $pathWithoutExportIgnore

[[ -e $pathWithoutExportIgnore/exclude-from-root ]]
[[ -e $pathWithoutExportIgnore/sub/exclude-from-sub ]]

# exportIgnore defaults to false when submodules = true
pathWithSubmodules=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$rootRepo; submodules = true; }).outPath")

[[ -e $pathWithoutExportIgnore/exclude-from-root ]]
[[ -e $pathWithoutExportIgnore/sub/exclude-from-sub ]]

test_submodule_nested() {
  local repoA=$TEST_ROOT/submodule_nested/a
  local repoB=$TEST_ROOT/submodule_nested/b
  local repoC=$TEST_ROOT/submodule_nested/c

  rm -rf "$repoA" "$repoB" "$repoC" "$TEST_HOME"/.cache/nix

  initGitRepo "$repoC"
  touch "$repoC"/inside-c
  git -C "$repoC" add inside-c
  addGitContent "$repoC"

  initGitRepo "$repoB"
  git -C "$repoB" submodule add "$repoC" c
  git -C "$repoB" add c
  addGitContent "$repoB"

  initGitRepo "$repoA"
  git -C "$repoA" submodule add "$repoB" b
  git -C "$repoA" add b
  addGitContent "$repoA"


  # Check non-worktree fetch
  local rev
  rev=$(git -C "$repoA" rev-parse HEAD)
  out=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repoA\"; rev = \"$rev\"; submodules = true; }).outPath")
  test -e "$out"/b/c/inside-c
  test -e "$out"/content
  test -e "$out"/b/content
  test -e "$out"/b/c/content
  local nonWorktree=$out

  # Check worktree based fetch
  # TODO: make it work without git submodule update
  git -C "$repoA" submodule update --init --recursive
  out=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repoA\"; submodules = true; }).outPath")
  find "$out"
  [[ $out == "$nonWorktree" ]] || { find "$out"; false; }

}
test_submodule_nested
