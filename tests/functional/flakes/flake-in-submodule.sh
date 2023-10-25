source common.sh

# Tests that:
# - flake.nix may reside inside of a git submodule
# - the flake can access content outside of the submodule
#
#   rootRepo
#   ├── root.nix
#   └── submodule
#       ├── flake.nix
#       └── sub.nix


requireGit

clearStore

# Submodules can't be fetched locally by default.
# See fetchGitSubmodules.sh
export XDG_CONFIG_HOME=$TEST_HOME/.config
git config --global protocol.file.allow always


rootRepo=$TEST_ROOT/rootRepo
subRepo=$TEST_ROOT/submodule


createGitRepo $subRepo
cat > $subRepo/flake.nix <<EOF
{
    outputs = { self }: {
        sub = import ./sub.nix;
        root = import ../root.nix;
    };
}
EOF
echo '"expression in submodule"' > $subRepo/sub.nix
git -C $subRepo add flake.nix sub.nix
git -C $subRepo commit -m Initial

createGitRepo $rootRepo

git -C $rootRepo submodule init
git -C $rootRepo submodule add $subRepo submodule
echo '"expression in root repo"' > $rootRepo/root.nix
git -C $rootRepo add root.nix
git -C $rootRepo commit -m "Add root.nix"

# FIXME
# Flake can live inside a submodule and can be accessed via ?dir=submodule
#[[ $(nix eval --json git+file://$rootRepo\?submodules=1\&dir=submodule#sub ) = '"expression in submodule"' ]]
# The flake can access content outside of the submodule
#[[ $(nix eval --json git+file://$rootRepo\?submodules=1\&dir=submodule#root ) = '"expression in root repo"' ]]
