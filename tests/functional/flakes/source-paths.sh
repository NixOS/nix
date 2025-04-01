#!/usr/bin/env bash

source ./common.sh

requireGit

repo=$TEST_ROOT/repo

createGitRepo "$repo"

cat > "$repo/flake.nix" <<EOF
{
  outputs = { ... }: {
    x = 1;
    y = assert false; 1;
  };
}
EOF

expectStderr 1 nix eval "$repo#x" | grepQuiet "error: path '$repo/flake.nix' does not exist"

git -C "$repo" add flake.nix

[[ $(nix eval "$repo#x") = 1 ]]

expectStderr 1 nix eval "$repo#y" | grepQuiet "at $repo/flake.nix:"

git -C "$repo" commit -a -m foo

expectStderr 1 nix eval "git+file://$repo?ref=master#y" | grepQuiet "at «git+file://$repo?ref=master&rev=.*»/flake.nix:"
