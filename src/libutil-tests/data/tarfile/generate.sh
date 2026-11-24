#!/usr/bin/env bash

# Make the tarballs in this directory, reproducibly. Their member names
# have `..` in them, which `tar` would not normally write, so to review
# them, run this and check that `git diff` shows no change.

set -eu -o pipefail

cd "$(dirname "$0")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo contents > "$work/b"
echo contents > "$work/c"
ln "$work/c" "$work/d"

tarOpts=(--format=ustar --mtime=@0 --owner=0 --group=0 --numeric-owner --sort=name -C "$work")

# An ordinary member, `a/b`.
tar -cf ok.tar "${tarOpts[@]}" --transform 's,^b$,a/b,' b

# A member named `a/../b`.
tar -cf dotdot.tar "${tarOpts[@]}" -P --transform 's,^b$,a/../b,' b

# A hard link `d`, whose target is `a/../c`.
tar -cf hardlink-dotdot.tar "${tarOpts[@]}" -P --transform 's,^c$,a/../c,RSh' c d
