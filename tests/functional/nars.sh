#!/usr/bin/env bash

source common.sh

clearStore

# Check that NARs with duplicate directory entries are rejected.
rm -rf "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "NAR directory is not sorted"

# Check that nix-store --restore fails if the output already exists.
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "path '.*/out/' already exists"

rm -rf "$TEST_ROOT/out"
echo foo > "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "cannot create directory.*File exists"

rm -rf "$TEST_ROOT/out"
ln -s "$TEST_ROOT/out2" "$TEST_ROOT/out"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "cannot create directory.*File exists"

mkdir -p "$TEST_ROOT/out2"
expectStderr 1 nix-store --restore "$TEST_ROOT/out" < duplicate.nar | grepQuiet "path '.*/out/' already exists"

# Check whether restoring and dumping a NAR that contains case
# collisions is round-tripping, even on a case-insensitive system.
rm -rf "$TEST_ROOT/case"
opts=("--option" "use-case-hack" "true")
nix-store "${opts[@]}" --restore "$TEST_ROOT/case" < case.nar
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
    expectStderr 1 nix-store --restore "$TEST_ROOT/out" < unnormalized.nar | grepQuiet "cannot create directory.*File exists"
else
    nix-store --restore "$TEST_ROOT/out" < unnormalized.nar
    [[ -e $TEST_ROOT/out/â ]]
    [[ -e $TEST_ROOT/out/â ]]
fi
