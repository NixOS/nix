#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# shellcheck disable=SC2016
outPath1=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo1"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)
# shellcheck disable=SC2016
outPath2=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo2"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)

TODO_NixOS # ignoring the client-specified setting 'auto-optimise-store', because it is a restricted setting and you are not a trusted user
  # TODO: only continue when trusted user or root

inode1="$(stat --format=%i "$outPath1"/foo)"
inode2="$(stat --format=%i "$outPath2"/foo)"
if [ "$inode1" != "$inode2" ]; then
    echo "inodes do not match"
    exit 1
fi

nlink="$(stat --format=%h "$outPath1"/foo)"
if [ "$nlink" != 3 ]; then
    echo "link count incorrect"
    exit 1
fi

# shellcheck disable=SC2016
outPath3=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo3"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link)

inode3="$(stat --format=%i "$outPath3"/foo)"
if [ "$inode1" = "$inode3" ]; then
    echo "inodes match unexpectedly"
    exit 1
fi

# XXX: This should work through the daemon too
NIX_REMOTE="" nix-store --optimise

inode1="$(stat --format=%i "$outPath1"/foo)"
inode3="$(stat --format=%i "$outPath3"/foo)"
if [ "$inode1" != "$inode3" ]; then
    echo "inodes do not match"
    exit 1
fi

nix-store --gc

if [ -n "$(ls "$NIX_STORE_DIR"/.links)" ]; then
    echo ".links directory not empty after GC"
    exit 1
fi

if [ -d "$NIX_STORE_DIR"/.links-b3 ] && [ -n "$(ls "$NIX_STORE_DIR"/.links-b3)" ]; then
    echo ".links-b3 directory not empty after GC"
    exit 1
fi

# Test BLAKE3-based deduplication
clearStoreIfPossible

export NIX_CONFIG="extra-experimental-features = blake3-links"

# Build two derivations with identical regular files, an executable, and a symlink
# shellcheck disable=SC2016
outPath1b3=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo1"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo; cp $out/foo $out/bar; chmod +x $out/bar; ln -s foo $out/lnk"; }' | nix-build - --no-out-link)
# shellcheck disable=SC2016
outPath2b3=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo2"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo; cp $out/foo $out/bar; chmod +x $out/bar; ln -s foo $out/lnk"; }' | nix-build - --no-out-link)

NIX_REMOTE="" nix-store --optimise

# Regular files should be deduplicated
inode1b3="$(stat --format=%i "$outPath1b3"/foo)"
inode2b3="$(stat --format=%i "$outPath2b3"/foo)"
if [ "$inode1b3" != "$inode2b3" ]; then
    echo "regular file inodes do not match after blake3 optimise"
    exit 1
fi

# Executable files should be deduplicated separately
inode1b3x="$(stat --format=%i "$outPath1b3"/bar)"
inode2b3x="$(stat --format=%i "$outPath2b3"/bar)"
if [ "$inode1b3x" != "$inode2b3x" ]; then
    echo "executable file inodes do not match after blake3 optimise"
    exit 1
fi

# Regular and executable should NOT be linked together (same content, different type)
if [ "$inode1b3" = "$inode1b3x" ]; then
    echo "regular and executable files should not share inodes"
    exit 1
fi

if [ ! -d "$NIX_STORE_DIR"/.links-b3 ]; then
    echo ".links-b3 directory was not created"
    exit 1
fi

# Check that link names have the correct suffixes
regularLinks=$(find "$NIX_STORE_DIR"/.links-b3/ -name '*-r' | wc -l)
executableLinks=$(find "$NIX_STORE_DIR"/.links-b3/ -name '*-x' | wc -l)
if [ "$regularLinks" -eq 0 ]; then
    echo "no -r suffixed links found"
    exit 1
fi
if [ "$executableLinks" -eq 0 ]; then
    echo "no -x suffixed links found"
    exit 1
fi

# Probe whether the filesystem supports hardlinking symlinks
probe_dir="$TEST_ROOT/link-probe"
mkdir -p "$probe_dir"
touch "$probe_dir/regular"
ln "$probe_dir/regular" "$probe_dir/hardlink" || { echo "hardlinks do not work at all"; exit 1; }
ln -s target "$probe_dir/symlink"
if ln "$probe_dir/symlink" "$probe_dir/symlink-hardlink" 2>/dev/null; then
    can_link_symlink=1
else
    can_link_symlink=0
fi
rm -rf "$probe_dir"

if [ "$can_link_symlink" = "1" ]; then
    inode1b3s="$(stat --format=%i "$outPath1b3"/lnk)"
    inode2b3s="$(stat --format=%i "$outPath2b3"/lnk)"
    if [ "$inode1b3s" != "$inode2b3s" ]; then
        echo "symlink inodes do not match after blake3 optimise"
        exit 1
    fi

    symlinkLinks=$(find "$NIX_STORE_DIR"/.links-b3/ -name '*-s' | wc -l)
    if [ "$symlinkLinks" -eq 0 ]; then
        echo "no -s suffixed links found"
        exit 1
    fi
fi

nix-store --gc

if [ -n "$(ls "$NIX_STORE_DIR"/.links-b3)" ]; then
    echo ".links-b3 directory not empty after GC"
    exit 1
fi
