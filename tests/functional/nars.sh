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

# Deserializing a NAR that contains file names that Unicode-normalize to the
# same name should fail on macOS and specific Linux setups (typically ZFS with
# `utf8only` enabled and `normalization` set to anything else than `none`). The
# deserialization should succeed on most Linux, where file names aren't
# unicode-normalized.
#
# We test that:
#
# 1. It either succeeds or fails with "already exists" error.
# 2. Nix has the same behavior with respect to unicode normalization than
#    $TEST_ROOT's filesystem (when using basic Unix commands)
rm -rf "$TEST_ROOT/out"
set +e
unicodeTestOut=$(nix-store --restore "$TEST_ROOT/out" < unnormalized.nar 2>&1)
unicodeTestCode=$?
set -e

touch "$TEST_ROOT/unicode-â" # non-canonical version
touch "$TEST_ROOT/unicode-â"

touchFilesCount=$(find "$TEST_ROOT" -maxdepth 1 -name "unicode-*" -type f | wc -l)

if (( unicodeTestCode == 1 )); then
    # If the command failed (MacOS or ZFS + normalization), checks that it failed
    # with the expected "already exists" error, and that this is the same
    # behavior as `touch`
    echo "$unicodeTestOut" | grepQuiet "path '.*/out/â' already exists"

    (( touchFilesCount == 1 ))
elif (( unicodeTestCode == 0 )); then
    # If the command succeeded, check that both files are present, and that this
    # is the same behavior as `touch`
    [[ -e $TEST_ROOT/out/â ]]
    [[ -e $TEST_ROOT/out/â ]]

    (( touchFilesCount == 2 ))
else
    # if the return code is neither 0 or 1, fail the test.
    echo "NAR deserialization of files with the same Unicode normalization failed with unexpected return code $unicodeTestCode" >&2
    exit 1
fi

rm -f "$TEST_ROOT/unicode-*"
