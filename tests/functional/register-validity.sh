#!/usr/bin/env bash

source common.sh

needLocalStore "uses nix-store --register-validity"

TODO_NixOS

clearStore

# Mutually referencing paths must be rejected and nothing registered.
a=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-a
b=$NIX_STORE_DIR/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb-b
touch "$a" "$b"

(echo "$a"; echo; echo 1; echo "$b"; echo "$b"; echo; echo 1; echo "$a") \
    | expectStderr 1 nix-store --register-validity | grepQuiet "cycle detected in the references"

expect 1 nix-store --check-validity "$a"
expect 1 nix-store --check-validity "$b"
