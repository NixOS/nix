#!/usr/bin/env bash

source ../common.sh

requireGit

repo=$TEST_ROOT/repo
link=$TEST_ROOT/link

createGitRepo "$repo"
ln -s "$repo" "$link"

# Dereferencing final symlink component should work and follow it to the directory.
# Also test various cases of symlink trickery in case that ever changes.
for path in {repo,link}{,/,/.} {repo,link}/.././repo{,/,/.}; do
    [ "$(nix-instantiate --eval --expr "builtins.readFileType (builtins.fetchTree \"git+file://$TEST_ROOT/$path\").outPath")" == '"directory"' ]
done
