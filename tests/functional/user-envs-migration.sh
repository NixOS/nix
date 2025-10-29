#!/usr/bin/env bash

# Test that the migration of user environments
# (https://github.com/NixOS/nix/pull/5226) does preserve everything

source common.sh

if isDaemonNewer "2.4pre20211005"; then
    skipTest "Daemon is too new"
fi


killDaemon
unset NIX_REMOTE

TODO_NixOS

clearStore
clearProfiles
rm -rf ~/.nix-profile

# Fill the environment using the older Nix
PATH_WITH_NEW_NIX="$PATH"
export PATH="$NIX_DAEMON_PACKAGE/bin:$PATH"

nix-env -f user-envs.nix -i foo-1.0
nix-env -f user-envs.nix -i bar-0.1

# Migrate to the new profile dir, and ensure that everything’s there
export PATH="$PATH_WITH_NEW_NIX"
nix-env -q # Trigger the migration
# shellcheck disable=SC2235
( [[ -L ~/.nix-profile ]] && \
    [[ $(readlink ~/.nix-profile) == ~/.local/share/nix/profiles/profile ]] ) || \
    fail "The nix profile should point to the new location"

(nix-env -q | grep foo && nix-env -q | grep bar && \
    [[ -e ~/.nix-profile/bin/foo ]] && \
    [[ $(nix-env --list-generations | wc -l) == 2 ]]) ||
    fail "The nix profile should have the same content as before the migration"
