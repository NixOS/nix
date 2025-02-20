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
    z = builtins.readFile ./foo;
  };
}
EOF

expectStderr 1 nix eval "$repo#x" | grepQuiet "error: Path 'flake.nix' in the repository \"$repo\" is not tracked by Git."

git -C "$repo" add flake.nix

[[ $(nix eval "$repo#x") = 1 ]]

expectStderr 1 nix eval "$repo#y" | grepQuiet "at $repo/flake.nix:"

git -C "$repo" commit -a -m foo

expectStderr 1 nix eval "git+file://$repo?ref=master#y" | grepQuiet "at «git+file://$repo?ref=master&rev=.*»/flake.nix:"

expectStderr 1 nix eval "$repo#z" | grepQuiet "error: Path 'foo' does not exist in Git repository \"$repo\"."
expectStderr 1 nix eval "git+file://$repo?ref=master#z" | grepQuiet "error: '«git+file://$repo?ref=master&rev=.*»/foo' does not exist"

echo 123 > "$repo/foo"

expectStderr 1 nix eval "$repo#z" | grepQuiet "error: Path 'foo' in the repository \"$repo\" is not tracked by Git."

git -C "$repo" add "$repo/foo"

[[ $(nix eval --raw "$repo#z") = 123 ]]
