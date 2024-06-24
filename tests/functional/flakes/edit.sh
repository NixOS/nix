#!/usr/bin/env bash

source ./common.sh

requireGit

flake1Dir=$TEST_ROOT/flake1

createGitRepo "$flake1Dir"
createSimpleGitFlake "$flake1Dir"

export EDITOR=cat
nix edit "$flake1Dir#" | grepQuiet simple.builder.sh
