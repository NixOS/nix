#!/usr/bin/env bash

source common.sh
TODO_NixOS

clearStore
clearCache

mkdir -p "$TEST_ROOT/stores"

outPath=$(nix-build --no-out-link dependencies.nix)

storeQueryParam="store=${NIX_STORE_DIR}"

remoteRoot="$TEST_ROOT/stores/legacy-ssh-pass"
chmod -R u+w "$remoteRoot" || true
rm -rf "$remoteRoot"

remoteStore="ssh://localhost?${storeQueryParam}&remote-program=nix-store&remote-store=${remoteRoot}%3f${storeQueryParam}%26real=${remoteRoot}${NIX_STORE_DIR}"

[ ! -f "${remoteRoot}${outPath}/foobar" ]
nix copy --to "$remoteStore" "$outPath"
[ -f "${remoteRoot}${outPath}/foobar" ]
