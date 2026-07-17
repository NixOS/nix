#!/usr/bin/env bash

# Regression test for the filesystem source accessor: statting or reading a
# file beneath a directory must require only *search* ("x") permission on the
# ancestor directories, not *read* ("r") permission.
#
# The fd-based PosixSourceAccessor once opened the parent directory `O_RDONLY`
# in `PosixDirectorySourceAccessor::openParent`, which requires read
# permission and broke stores whose backing directory is deliberately
# traversable but not listable (e.g. `/nix/store` hardened to mode `--x`, as
# used with a remote store's `real` directory). It should only require
# traversal, matching the old path-based `lstat` behaviour.

source common.sh

# `chmod`-based; the read-only bits don't behave as expected under the NixOS
# test sandbox.
TODO_NixOS

# The permission distinction (traverse vs. list) is only enforced where the
# accessor uses `O_PATH` (Linux). Elsewhere it still opens ancestors `O_RDONLY`.
[[ "$(uname -s)" = Linux ]] || skipTest "requires O_PATH (Linux)"

# Not meaningful as root, which bypasses directory permission checks.
[[ "$(id -u)" != 0 ]] || skipTest "does not work as root"

dir="$TEST_ROOT/traverse-only"
rm -rf "$dir"
mkdir -p "$dir/sub"
echo -n "hi there" > "$dir/sub/file"

# Make the parent (and an intermediate) directory traversable but not listable.
chmod 0111 "$dir/sub"
chmod 0111 "$dir"

cleanup() { chmod -R u+rwx "$dir" 2>/dev/null || true; }
trap cleanup EXIT

# `maybeLstat` through the accessor: existence checks must not list the dir.
[[ "$(nix eval --impure --expr "builtins.pathExists $dir/sub/file")" = true ]]

# `readFile` through the accessor: reading the file needs read on the file and
# only traversal on its ancestors.
[[ "$(nix eval --impure --raw --expr "builtins.readFile $dir/sub/file")" = "hi there" ]]
