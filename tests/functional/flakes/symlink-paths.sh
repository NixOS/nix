#!/usr/bin/env bash

source ./common.sh

requireGit

create_flake() {
    local flakeDir="$1"
    createGitRepo "$flakeDir"
    cat > "$flakeDir/flake.nix" <<EOF
{
    outputs = { self }: { x = 2; };
}
EOF
    git -C "$flakeDir" add flake.nix
    git -C "$flakeDir" commit -m Initial
}

test_symlink_points_to_flake() {
    create_flake "$TEST_ROOT/flake1"
    ln -sn "$TEST_ROOT/flake1" "$TEST_ROOT/flake1_sym"
    [[ $(nix eval "$TEST_ROOT/flake1_sym#x") = 2 ]]
    rm -rf "$TEST_ROOT/flake1" "$TEST_ROOT/flake1_sym"
}
test_symlink_points_to_flake

test_symlink_points_to_flake_in_subdir() {
    create_flake "$TEST_ROOT/subdir/flake1"
    ln -sn "$TEST_ROOT/subdir" "$TEST_ROOT/subdir_sym"
    [[ $(nix eval "$TEST_ROOT/subdir_sym/flake1#x") = 2 ]]
    rm -rf "$TEST_ROOT/subdir" "$TEST_ROOT/subdir_sym"
}
test_symlink_points_to_flake_in_subdir

test_symlink_points_to_dir_in_repo() {
    local repoDir="$TEST_ROOT/flake1"
    createGitRepo "$repoDir"
    mkdir -p "$repoDir/subdir"
    cat > "$repoDir/subdir/flake.nix" <<EOF
{
    outputs = { self }: { x = 2; };
}
EOF
    git -C "$repoDir" add subdir/flake.nix
    git -C "$repoDir" commit -m Initial
    ln -sn "$TEST_ROOT/flake1/subdir" "$TEST_ROOT/flake1_sym"
    [[ $(nix eval "$TEST_ROOT/flake1_sym#x") = 2 ]]
    rm -rf "$TEST_ROOT/flake1" "$TEST_ROOT/flake1_sym"
}
test_symlink_points_to_dir_in_repo

test_symlink_from_repo_to_another() {
    local repoDir="$TEST_ROOT/repo1"
    createGitRepo "$repoDir"
    echo "Hello" > "$repoDir/file"
    mkdir "$repoDir/subdir"
    cat > "$repoDir/subdir/flake.nix" <<EOF
{
    outputs = { self }: { x = builtins.readFile ../file; };
}
EOF
    git -C "$repoDir" add subdir/flake.nix file
    git -C "$repoDir" commit -m Initial
    [[ $(nix eval "$TEST_ROOT/repo1/subdir#x") == \"Hello\\n\" ]]

    local repo2Dir="$TEST_ROOT/repo2"
    createGitRepo "$repo2Dir"
    ln -sn "$repoDir/subdir" "$repo2Dir/flake1_sym"
    echo "World" > "$repo2Dir/file"
    git -C "$repo2Dir" add flake1_sym file
    git -C "$repo2Dir" commit -m Initial
    [[ $(nix eval "$repo2Dir/flake1_sym#x") == \"Hello\\n\" ]]
    rm -rf "$TEST_ROOT/repo1" "$TEST_ROOT/repo2"
}
test_symlink_from_repo_to_another
