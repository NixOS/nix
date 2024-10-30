#!/usr/bin/env bash

source common.sh

# XXX: This shouldn’t be, but #4813 cause this test to fail
buggyNeedLocalStore "see #4813"

checkBuildTempDirRemoved ()
{
    buildDir=$(sed -n 's/CHECK_TMPDIR=//p' "$1" | head -1)
    checkBuildIdFile=${buildDir}/checkBuildId
    [[ ! -f $checkBuildIdFile ]] || ! grep "$checkBuildId" "$checkBuildIdFile"
}

# written to build temp directories to verify created by this instance
checkBuildId=$(date +%s%N)

TODO_NixOS

clearStore

nix-build dependencies.nix --no-out-link
nix-build dependencies.nix --no-out-link --check

# Build failure exit codes (100, 104, etc.) are from
# doc/manual/source/command-ref/status-build-failure.md

# check for dangling temporary build directories
# only retain if build fails and --keep-failed is specified, or...
# ...build is non-deterministic and --check and --keep-failed are both specified
nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
    --no-out-link 2> "$TEST_ROOT/log" || status=$?
[ "$status" = "100" ]
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
    --no-out-link --keep-failed 2> "$TEST_ROOT/log" || status=$?
[ "$status" = "100" ]
if checkBuildTempDirRemoved "$TEST_ROOT/log"; then false; fi

test_custom_build_dir() {
  local customBuildDir="$TEST_ROOT/custom-build-dir"

  # Nix does not create the parent directories, and perhaps it shouldn't try to
  # decide the permissions of build-dir.
  mkdir "$customBuildDir"
  nix-build check.nix -A failed --argstr checkBuildId "$checkBuildId" \
      --no-out-link --keep-failed --option build-dir "$TEST_ROOT/custom-build-dir" 2> "$TEST_ROOT/log" || status=$?
  [ "$status" = "100" ]
  [[ 1 == "$(count "$customBuildDir/nix-build-"*)" ]]
  local buildDir=("$customBuildDir/nix-build-"*)
  if [[ -e ${buildDir[*]}/build ]]; then
      buildDir[0]="${buildDir[*]}/build"
  fi
  grep "$checkBuildId" "${buildDir[*]}/checkBuildId"
}
test_custom_build_dir

nix-build check.nix -A deterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link 2> "$TEST_ROOT/log"
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A deterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link --check --keep-failed 2> "$TEST_ROOT/log"
if grepQuiet 'may not be deterministic' "$TEST_ROOT/log"; then false; fi
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A nondeterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link 2> "$TEST_ROOT/log"
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A nondeterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link --check 2> "$TEST_ROOT/log" || status=$?
grep 'may not be deterministic' "$TEST_ROOT/log"
[ "$status" = "104" ]
checkBuildTempDirRemoved "$TEST_ROOT/log"

nix-build check.nix -A nondeterministic --argstr checkBuildId "$checkBuildId" \
    --no-out-link --check --keep-failed 2> "$TEST_ROOT/log" || status=$?
grep 'may not be deterministic' "$TEST_ROOT/log"
[ "$status" = "104" ]
if checkBuildTempDirRemoved "$TEST_ROOT/log"; then false; fi

TODO_NixOS

clearStore

path=$(nix-build check.nix -A fetchurl --no-out-link)

chmod +w "$path"
echo foo > "$path"
chmod -w "$path"

nix-build check.nix -A fetchurl --no-out-link --check
# Note: "check" doesn't repair anything, it just compares to the hash stored in the database.
[[ $(cat "$path") = foo ]]

nix-build check.nix -A fetchurl --no-out-link --repair
[[ $(cat "$path") != foo ]]

echo 'Hello World' > "$TEST_ROOT/dummy"
nix-build check.nix -A hashmismatch --no-out-link || status=$?
[ "$status" = "102" ]

echo -n > "$TEST_ROOT/dummy"
nix-build check.nix -A hashmismatch --no-out-link
echo 'Hello World' > "$TEST_ROOT/dummy"

nix-build check.nix -A hashmismatch --no-out-link --check || status=$?
[ "$status" = "102" ]

# Multiple failures with --keep-going
nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic -A hashmismatch --no-out-link --check --keep-going || status=$?
[ "$status" = "110" ]
