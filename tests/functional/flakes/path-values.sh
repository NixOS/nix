#!/usr/bin/env bash

# See https://github.com/NixOS/nix/issues/16550.

source ./common.sh

requireGit

repo="$TEST_ROOT/repo"

# Git because we want lazily fetched trees.
createGitRepo "$repo"

cd "$repo"
printf '#!/bin/sh\necho hello\n' >run.sh
chmod +x run.sh

cat >flake.nix <<EOF
{
  outputs = { self }: {
    toString = toString ./data.txt;
    toXML = builtins.toXML ./data.txt;
    apps.$system.default = { type = "app"; program = ./run.sh; };
  };
}
EOF

git add -A
git commit -qm init

storeDir=$(nix-instantiate --eval -E 'builtins.storeDir' --raw)

nix eval --raw .#toString | grepQuiet "$storeDir/.*-source/data.txt"
nix eval --raw .#toXML | grepQuiet "$storeDir/.*-source/data.txt"
nix run . | grepQuiet "hello"
