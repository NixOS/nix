#!/usr/bin/env bash

source common.sh

path1=$(nix-store --add ./dummy)
echo "$path1"

path2=$(nix-store --add-fixed sha256 --recursive ./dummy)
echo "$path2"

if test "$path1" != "$path2"; then
    echo "nix-store --add and --add-fixed mismatch"
    exit 1
fi

path3=$(nix-store --add-fixed sha256 ./dummy)
echo "$path3"
test "$path1" != "$path3" || exit 1

path4=$(nix-store --add-fixed sha1 --recursive ./dummy)
echo "$path4"
test "$path1" != "$path4" || exit 1

hash1=$(nix-store -q --hash "$path1")
echo "$hash1"

hash2=$(nix-hash --type sha256 --base32 ./dummy)
echo "$hash2"

test "$hash1" = "sha256:$hash2"

# The contents can be accessed through a symlink, and this symlink has no effect on the hash
# https://github.com/NixOS/nix/issues/11941
test_issue_11941() {
    local expected actual
    mkdir -p "$TEST_ROOT/foo/bar" && ln -s "$TEST_ROOT/foo" "$TEST_ROOT/foo-link"

    # legacy
    expected=$(nix-store --add-fixed --recursive sha256 "$TEST_ROOT/foo/bar")
    actual=$(nix-store --add-fixed --recursive sha256 "$TEST_ROOT/foo-link/bar")
    [[ "$expected" == "$actual" ]]
    actual=$(nix-store --add "$TEST_ROOT/foo-link/bar")
    [[ "$expected" == "$actual" ]]

    # nix store add
    actual=$(nix store add --hash-algo sha256 --mode nar "$TEST_ROOT/foo/bar")
    [[ "$expected" == "$actual" ]]

    # cleanup
    rm -r "$TEST_ROOT/foo" "$TEST_ROOT/foo-link"
}
test_issue_11941

# A symlink is added to the store as a symlink, not as a copy of the target
test_add_symlink() {
    ln -s /bin "$TEST_ROOT/my-bin"

    # legacy
    path=$(nix-store --add-fixed --recursive sha256 "$TEST_ROOT/my-bin")
    [[ "$(readlink "$path")" == /bin ]]
    path=$(nix-store --add "$TEST_ROOT/my-bin")
    [[ "$(readlink "$path")" == /bin ]]

    # nix store add
    path=$(nix store add --hash-algo sha256 --mode nar "$TEST_ROOT/my-bin")
    [[ "$(readlink "$path")" == /bin ]]

    # cleanup
    rm "$TEST_ROOT/my-bin"
}
test_add_symlink

#### New style commands

clearStoreIfPossible

(
    path1=$(nix store add ./dummy)
    path2=$(nix store add --mode nar ./dummy)
    path3=$(nix store add-path ./dummy)
    [[ "$path1" == "$path2" ]]
    [[ "$path1" == "$path3" ]]
    path4=$(nix store add --mode nar --hash-algo sha1 ./dummy)
)
(
    path1=$(nix store add --mode flat ./dummy)
    path2=$(nix store add-file ./dummy)
    [[ "$path1" == "$path2" ]]
    path4=$(nix store add --mode flat --hash-algo sha1 ./dummy)
)
(
    path1=$(nix store add --mode text ./dummy)
    path2=$(nix eval --impure --raw --expr 'builtins.toFile "dummy" (builtins.readFile ./dummy)')
    [[ "$path1" == "$path2" ]]
)
