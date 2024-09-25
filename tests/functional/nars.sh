#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

# Check that NARs with duplicate directory entries are rejected.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "NAR directory is not sorted"

# Check that nix-store --restore fails if the output already exists.
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "path '.*/out' already exists"

rm -rf "$TEST_ROOT/out"
echo foo > "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "File exists"

rm -rf "$TEST_ROOT/out"
ln -s "$TEST_ROOT/out2" "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "File exists"

mkdir -p "$TEST_ROOT/out2"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "path '.*/out' already exists"

# The same, but for a regular file.
nix-store --dump ./nars.sh > "$TEST_ROOT/tmp.nar"

rm -rf "$TEST_ROOT/out"
nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

rm -rf "$TEST_ROOT/out"
mkdir -p "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

rm -rf "$TEST_ROOT/out"
ln -s "$TEST_ROOT/out2" "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

mkdir -p "$TEST_ROOT/out2"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

# The same, but for a symlink.
ln -sfn foo "$TEST_ROOT/symlink"
nix-store --dump "$TEST_ROOT/symlink" > "$TEST_ROOT/tmp.nar"

rm -rf "$TEST_ROOT/out"
nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar"
[[ -L "$TEST_ROOT/out" ]]
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

rm -rf "$TEST_ROOT/out"
mkdir -p "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

rm -rf "$TEST_ROOT/out"
ln -s "$TEST_ROOT/out2" "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

mkdir -p "$TEST_ROOT/out2"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < "$TEST_ROOT/tmp.nar" | grepQuiet "File exists"

# Check whether restoring and dumping a NAR that contains case
# collisions is round-tripping, even on a case-insensitive system.
rm -rf "$TEST_ROOT/case"
opts=("--option" "use-case-hack" "true")
nix-store "${opts[@]}" --restore "$TEST_ROOT/case" < case.nar
[[ -e "$TEST_ROOT/case/xt_CONNMARK.h" ]]
[[ -e "$TEST_ROOT/case/xt_CONNmark.h~nix~case~hack~1" ]]
[[ -e "$TEST_ROOT/case/xt_connmark.h~nix~case~hack~2" ]]
[[ -e "$TEST_ROOT/case/x/FOO" ]]
[[ -d "$TEST_ROOT/case/x/Foo~nix~case~hack~1" ]]
[[ -e "$TEST_ROOT/case/x/foo~nix~case~hack~2/a~nix~case~hack~1/foo" ]]
nix-store "${opts[@]}" --dump "$TEST_ROOT/case" > "$TEST_ROOT/case.nar"
cmp case.nar "$TEST_ROOT/case.nar"
[ "$(nix-hash "${opts[@]}" --type sha256 "$TEST_ROOT/case")" = "$(nix-hash --flat --type sha256 case.nar)" ]

# Check whether we detect true collisions (e.g. those remaining after
# removal of the suffix).
touch "$TEST_ROOT/case/xt_CONNMARK.h~nix~case~hack~3"
(! nix-store "${opts[@]}" --dump "$TEST_ROOT/case" > /dev/null)

# Detect NARs that have a directory entry that after case-hacking
# collides with another entry (e.g. a directory containing 'Test',
# 'Test~nix~case~hack~1' and 'test').
rm -rf "$TEST_ROOT/case"
expectStderr 1 nix-store "${opts[@]}" --restore "$TEST_ROOT/case" < case-collision.nar | grepQuiet "NAR contains file name 'test' that collides with case-hacked file name 'Test~nix~case~hack~1'"

# Deserializing a NAR that contains file names that Unicode-normalize
# to the same name should fail on macOS but succeed on Linux.
rm -rf "$TEST_ROOT/out"
if [[ $(uname) = Darwin ]]; then
    expectStderr 1 nix-store --restore "$TEST_ROOT/out" < unnormalized.nar | grepQuiet "path '.*/out/â' already exists"
else
    nix-store --restore "$TEST_ROOT/out" < unnormalized.nar
    [[ -e $TEST_ROOT/out/â ]]
    [[ -e $TEST_ROOT/out/â ]]
fi

# Unpacking a NAR with a NUL character in a file name should fail.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < nul.nar | grepQuiet "NAR contains invalid file name 'f"

# Likewise for a '.' filename.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < dot.nar | grepQuiet "NAR contains invalid file name '.'"

# Likewise for a '..' filename.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < dotdot.nar | grepQuiet "NAR contains invalid file name '..'"

# Likewise for a filename containing a slash.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < slash.nar | grepQuiet "NAR contains invalid file name 'x/y'"

# Likewise for an empty filename.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < empty.nar | grepQuiet "NAR contains invalid file name ''"

# Test that the 'executable' field cannot come before the 'contents' field.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < executable-after-contents.nar | grepQuiet "expected tag ')', got 'executable'"

# Test that the 'name' field cannot come before the 'node' field in a directory entry.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < name-after-node.nar | grepQuiet "expected tag 'name'"
