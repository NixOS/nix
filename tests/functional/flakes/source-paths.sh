#!/usr/bin/env bash

source ./common.sh

requireGit

repo=$TEST_ROOT/repo

createGitRepo "$repo"

cat > "$repo/flake.nix" <<EOF
{
  outputs = { ... }: {
    x = 1;
  };
}
EOF

expectStderr 1 nix eval "$repo#x" | grepQuiet "error: Path 'flake.nix' in the repository \"$repo\" is not tracked by Git."

git -C "$repo" add flake.nix

[[ $(nix eval "$repo#x") = 1 ]]
