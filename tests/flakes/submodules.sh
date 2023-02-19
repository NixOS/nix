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

# Submodules can't be fetched locally by default, which can cause
# information leakage vulnerabilities, but for these tests our
# submodule is intentionally local and it's all trusted, so we
# disable this restriction. Setting it per repo is not sufficient, as
# the repo-local config does not apply to the commands run from
# outside the repos by Nix.
export XDG_CONFIG_HOME=$TEST_HOME/.config
git config --global protocol.file.allow always

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

cd $rootRepo

touch flake.nix
git add flake.nix

# Case, CLI parameter
cat <<EOF > flake.nix
{
    outputs = {self}: {
      sub = self.outPath;
    };
}
EOF
[ -e  $(nix eval '.?submodules=1#sub' --raw)/sub/content ]

# Case, CLI parameter
cat <<EOF > flake.nix
{
    outputs = {self}: {
      sub = self.outPath;
    };
}
EOF
[ ! -e  $(nix eval '.?submodules=0#sub' --raw)/sub/content ]

# Case, flake.nix parameter
cat <<EOF > flake.nix
{
    inputs.self.submodules = false;
    outputs = {self}: {
      sub = self.outPath;
    };
}
EOF

[ ! -e  $(nix eval .#sub --raw)/sub/content ]

# Case, flake.nix parameter
cat <<EOF > flake.nix
{
    inputs.self.submodules = true;
    outputs = {self}: {
      sub = self.outPath;
    };
}
EOF
[ -e  $(nix eval .#sub --raw)/sub/content ]

# Case, CLI precedence
cat <<EOF > flake.nix
{
    inputs.self.submodules = true;
    outputs = {self}: {
      sub = self.outPath;
    };
}
EOF
[ ! -e  $(nix eval '.?submodules=0#sub' --raw)/sub/content ]

# Case, CLI precedence
cat <<EOF > flake.nix
{
    inputs.self.submodules = false;
    outputs = {self}: {
      sub = self.outPath;
    };
}
EOF
[ -e  $(nix eval '.?submodules=1#sub' --raw)/sub/content ]
