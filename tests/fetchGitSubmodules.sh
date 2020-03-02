source common.sh

set -u

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping Git submodule tests"
    exit 99
fi

clearStore

rootRepo=$TEST_ROOT/gitSubmodulesRoot
subRepo=$TEST_ROOT/gitSubmodulesSub

rm -rf ${rootRepo} ${subRepo} $TEST_HOME/.cache/nix/gitv2

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

pathWithoutSubmodules=$(nix eval --raw "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; }).outPath")
pathWithSubmodules=$(nix eval --raw "(builtins.fetchGit { url = file://$rootRepo; rev = \"$rev\"; fetchSubmodules = true; }).outPath")

# The resulting store path cannot be the same.
[[ $pathWithoutSubmodules != $pathWithSubmodules ]]

[[ ! -e $pathWithoutSubmodules/sub/content ]]
[[ -e $pathWithSubmodules/sub/content ]]

# No .git directory or submodule reference files must be left
test "$(find "$pathWithSubmodules" -name .git)" = ""
