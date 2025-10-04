#!/usr/bin/env bash

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

TODO_NixOS

clearStore

# Submodules can't be fetched locally by default.
# See fetchGitSubmodules.sh
export XDG_CONFIG_HOME=$TEST_HOME/.config
git config --global protocol.file.allow always


rootRepo=$TEST_ROOT/rootRepo
subRepo=$TEST_ROOT/submodule
otherRepo=$TEST_ROOT/otherRepo


createGitRepo "$subRepo"
cat > "$subRepo"/flake.nix <<EOF
{
    outputs = { self }: {
        sub = import ./sub.nix;
        root = import ../root.nix;
    };
}
EOF
echo '"expression in submodule"' > "$subRepo"/sub.nix
git -C "$subRepo" add flake.nix sub.nix
git -C "$subRepo" commit -m Initial

createGitRepo "$rootRepo"

git -C "$rootRepo" submodule init
git -C "$rootRepo" submodule add "$subRepo" submodule
echo '"expression in root repo"' > "$rootRepo"/root.nix
git -C "$rootRepo" add root.nix
git -C "$rootRepo" commit -m "Add root.nix"

flakeref=git+file://$rootRepo\?submodules=1\&dir=submodule

# Flake can live inside a submodule and can be accessed via ?dir=submodule
[[ $(nix eval --json "$flakeref#sub" ) = '"expression in submodule"' ]]

# The flake can access content outside of the submodule
[[ $(nix eval --json "$flakeref#root" ) = '"expression in root repo"' ]]

# Check that dirtying a submodule makes the entire thing dirty.
[[ $(nix flake metadata --json "$flakeref" | jq -r .locked.rev) != null ]]
echo '"foo"' > "$rootRepo"/submodule/sub.nix
[[ $(_NIX_TEST_BARF_ON_UNCACHEABLE='' nix eval --json "$flakeref#sub" ) = '"foo"' ]]
[[ $(_NIX_TEST_BARF_ON_UNCACHEABLE='' nix flake metadata --json "$flakeref" | jq -r .locked.rev) = null ]]

# Test that `nix flake metadata` parses `submodule` correctly.
cat > "$rootRepo"/flake.nix <<EOF
{
    outputs = { self }: {
    };
}
EOF
git -C "$rootRepo" add flake.nix
git -C "$rootRepo" commit -m "Add flake.nix"

storePath=$(_NIX_TEST_BARF_ON_UNCACHEABLE='' nix flake prefetch --json "$rootRepo?submodules=1" | jq -r .storePath)
[[ -e "$storePath/submodule" ]]

# Test the use of inputs.self.
cat > "$rootRepo"/flake.nix <<EOF
{
  inputs.self.submodules = true;
  outputs = { self }: {
    foo = self.outPath;
  };
}
EOF
git -C "$rootRepo" commit -a -m "Bla"

storePath=$(nix eval --raw "$rootRepo#foo")
[[ -e "$storePath/submodule" ]]


# Test another repo referring to a repo that uses inputs.self.
createGitRepo "$otherRepo"
cat > "$otherRepo"/flake.nix <<EOF
{
  inputs.root.url = "git+file://$rootRepo";
  outputs = { self, root }: {
    foo = root.foo;
  };
}
EOF
git -C "$otherRepo" add flake.nix

# The first call should refetch the root repo...
expectStderr 0 nix eval --raw "$otherRepo#foo" -vvvvv | grepQuiet "refetching"

[[ $(jq .nodes.root_2.locked.submodules "$otherRepo/flake.lock") == true ]]

# ... but the second call should have 'submodules = true' in flake.lock, so it should not refetch.
rm -rf "$TEST_HOME/.cache"
clearStore
expectStderr 0 nix eval --raw "$otherRepo#foo" -vvvvv | grepQuietInverse "refetching"

storePath=$(nix eval --raw "$otherRepo#foo")
[[ -e "$storePath/submodule" ]]


# The root repo may use the submodule repo as an input
# through the relative path. This may change in the future;
# see: https://discourse.nixos.org/t/57783 and #9708.
cat > "$rootRepo"/flake.nix <<EOF
{
    inputs.subRepo.url = "git+file:./submodule";
    outputs = { ... }: { };
}
EOF
git -C "$rootRepo" add flake.nix
git -C "$rootRepo" commit -m "Add subRepo input"
(
  cd "$rootRepo"
  # The submodule must be locked to the relative path,
  # _not_ the absolute path:
  [[ $(nix flake metadata --json | jq -r .locks.nodes.subRepo.locked.url) = "file:./submodule" ]]
)
