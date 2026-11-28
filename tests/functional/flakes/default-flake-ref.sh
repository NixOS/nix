#!/usr/bin/env bash

source common.sh

requireGit

pkgsDir="$TEST_ROOT/default-flake-ref-pkgs"
createGitRepo "$pkgsDir"
createSimpleGitFlake "$pkgsDir"

nix registry add --registry "$registry" mypkgs "git+file://$pkgsDir"

cd "$TEST_HOME"
flags=(--flake-registry "$registry" --no-link)

# A bare name is looked up in the flake given by 'default-flake-ref'.
nix build "${flags[@]}" --default-flake-ref mypkgs foo
nix build "${flags[@]}" --default-flake-ref mypkgs hello # via 'legacyPackages'
nix build "${flags[@]}" --default-flake-ref "git+file://$pkgsDir" foo

# Extended output specifiers keep working on bare names.
nix build "${flags[@]}" --default-flake-ref mypkgs 'foo^out'

# The setting can be set through the configuration, too.
NIX_CONFIG="default-flake-ref = mypkgs" nix build "${flags[@]}" foo

# An explicit installable always takes precedence.
nix build "${flags[@]}" --default-flake-ref nonexistent mypkgs#foo

# A bare registry name keeps referring to the whole flake.
nix build "${flags[@]}" --default-flake-ref nonexistent mypkgs

# An empty value disables the feature.
expectStderr 1 nix build "${flags[@]}" --default-flake-ref '' foo | grepQuiet "foo"

# An existing file or directory wins over the default.
mkdir foo
expectStderr 1 nix build "${flags[@]}" --default-flake-ref mypkgs foo | grepQuiet "foo"
rmdir foo
