#!/usr/bin/env bash

source common.sh

TODO_NixOS

# Test `--repair-path` through the daemon; repair.sh needs a local store.
startDaemon

path=$(nix-build dependencies.nix -o "$TEST_ROOT"/result)
path2=$(nix-store -qR "$path" | grep input-2)
hash=$(nix-hash "$path2")
corrupt() { chmod u+w "$path2"; touch "$path2"/bad; }

# The daemon ignores the client's `--no-require-sigs`, so sign the cache.
nix key generate-secret --key-name test.nixos.org-1 > "$TEST_ROOT/sk1"
publicKey=$(nix key convert-secret-to-public < "$TEST_ROOT/sk1")
nix copy --to "file://$cacheDir?secret-key=$TEST_ROOT/sk1" "$path"

# Without a substituter, the path is repaired by rebuilding its deriver.
corrupt
nix-store --repair-path "$path2"
[[ "$(nix-hash "$path2")" == "$hash" && ! -e "$path2"/bad ]]

# Delete the deriver, so that substituting is the only way to repair.
# shellcheck disable=SC2046
nix-store --delete $(nix-store -q --referrers-closure "$(nix-store -qd "$path2")")
corrupt
nix-store --repair-path "$path2" --substituters "file://$cacheDir" --trusted-public-keys "$publicKey"
[[ "$(nix-hash "$path2")" == "$hash" && ! -e "$path2"/bad ]]

# With neither a substituter nor a deriver, the path can't be repaired.
corrupt
expectStderr 1 nix-store --repair-path "$path2" | grepQuiet "cannot repair path '$path2'"

# Repairing is not allowed for untrusted clients.
sed -i -e '/^trusted-users =/d' "$test_nix_conf"
restartDaemon
expectStderr 1 nix-store --repair-path "$path2" | grepQuiet "repairing is not allowed because you are not in 'trusted-users'"

killDaemon
