source common.sh

set -u

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping Git submodule tests"
    exit 99
fi

clearStore

rootRepo=$TEST_ROOT/gitSubmodulesRoot
subRepo=$TEST_ROOT/gitSubmodulesSub

rm -rf ${rootRepo} ${subRepo} $TEST_HOME/.cache/nix

initGitRepo() {
    git init $1
    git -C $1 config user.email "foobar@example.com"
    git -C $1 config user.name "Foobar"
}

addGitContent() {
    echo "lorem ipsum" > $1/content
    git -C $1 add content
    git -C $1 commit -m "Initial commit"
}

initGitRepo $subRepo
addGitContent $subRepo

initGitRepo $rootRepo

git -C $rootRepo submodule init
git -C $rootRepo submodule add $subRepo sub
git -C $rootRepo add sub
git -C $rootRepo commit -m "Add submodule"

rev=$(git -C $rootRepo rev-parse HEAD)

r1=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; }).outPath")
r2=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = false; }).outPath")
r3=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; submodules = true; }).outPath")

[[ $r1 == $r2 ]]
[[ $r2 != $r3 ]]

r4=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; }).outPath")
r5=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = false; }).outPath")
r6=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = true; }).outPath")
r7=$(nix eval --raw --expr "(builtins.fetchGit { url = $rootRepo; ref = \"master\"; rev = \"$rev\"; submodules = true; }).outPath")
r8=$(nix eval --raw --expr "(builtins.fetchGit { url = $rootRepo; rev = \"$rev\"; submodules = true; }).outPath")

[[ $r1 == $r4 ]]
[[ $r4 == $r5 ]]
[[ $r3 == $r6 ]]
[[ $r6 == $r7 ]]
[[ $r7 == $r8 ]]

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
[[ $pathWithoutSubmodules != $pathWithSubmodules ]]

# Checking out the same repo with submodules returns in the same store path.
[[ $pathWithSubmodules == $pathWithSubmodulesAgain ]]

# Checking out the same repo with submodules returns in the same store path.
[[ $pathWithSubmodulesAgain == $pathWithSubmodulesAgainWithRef ]]

# The submodules flag is actually honored.
[[ ! -e $pathWithoutSubmodules/sub/content ]]
[[ -e $pathWithSubmodules/sub/content ]]

[[ -e $pathWithSubmodulesAgainWithRef/sub/content ]]

# No .git directory or submodule reference files must be left
test "$(find "$pathWithSubmodules" -name .git)" = ""

# Git repos without submodules can be fetched with submodules = true.
subRev=$(git -C $subRepo rev-parse HEAD)
noSubmoduleRepoBaseline=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$subRepo; rev = \"$subRev\"; }).outPath")
noSubmoduleRepo=$(nix eval --raw --expr "(builtins.fetchGit { url = file://$subRepo; rev = \"$subRev\"; submodules = true; }).outPath")

[[ $noSubmoduleRepoBaseline == $noSubmoduleRepo ]]
