#!/usr/bin/env bash
source ./common.sh

requireGit

flake1Dir="$TEST_ROOT/eval-cache-flake"

createGitRepo "$flake1Dir" ""

cat >"$flake1Dir/flake.nix" <<EOF
{
  description = "Fnord";
  outputs = { self }: {
    foo.bar = throw "breaks";
  };
}
EOF

git -C "$flake1Dir" add flake.nix
git -C "$flake1Dir" commit -m "Init"

expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: breaks'
expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: breaks'
