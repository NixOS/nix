#!/usr/bin/env bash

# Test the function for lang.sh
source common.sh

source characterisation/framework.sh

# We are testing this, so don't want outside world to affect us.
unset _NIX_TEST_ACCEPT

# We'll only modify this in subshells so we don't need to reset it.
badDiff=0

# matches non-empty
echo Hi! > "$TEST_ROOT/got"
cp "$TEST_ROOT/got" "$TEST_ROOT/expected"
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 0 ))
)

# matches empty, non-existant file is the same as empty file
echo -n > "$TEST_ROOT/got"
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/does-not-exist"
  (( "$badDiff" == 0 ))
)

# doesn't matches non-empty, non-existant file is the same as empty file
echo Hi! > "$TEST_ROOT/got"
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/does-not-exist"
  (( "$badDiff" == 1 ))
)

# doesn't match, `badDiff` set, file unchanged
echo Hi! > "$TEST_ROOT/got"
echo Bye! > "$TEST_ROOT/expected"
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 1 ))
)
[[ "Bye!" == $(< "$TEST_ROOT/expected") ]]

# _NIX_TEST_ACCEPT=1 matches non-empty
echo Hi! > "$TEST_ROOT/got"
cp "$TEST_ROOT/got" "$TEST_ROOT/expected"
(
  _NIX_TEST_ACCEPT=1 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 0 ))
)

# _NIX_TEST_ACCEPT doesn't match, `badDiff=1` set, file changed (was previously non-empty)
echo Hi! > "$TEST_ROOT/got"
echo Bye! > "$TEST_ROOT/expected"
(
  _NIX_TEST_ACCEPT=1 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 1 ))
)
[[ "Hi!" == $(< "$TEST_ROOT/expected") ]]
# second time succeeds
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 0 ))
)

# _NIX_TEST_ACCEPT matches empty, non-existant file not created
echo -n > "$TEST_ROOT/got"
(
  _NIX_TEST_ACCEPT=1 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/does-not-exists"
  (( "$badDiff" == 0 ))
)
[[ ! -f "$TEST_ROOT/does-not-exist" ]]

# _NIX_TEST_ACCEPT doesn't match, output empty, file deleted
echo -n > "$TEST_ROOT/got"
echo Bye! > "$TEST_ROOT/expected"
badDiff=0
(
  _NIX_TEST_ACCEPT=1 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 1 ))
)
[[ ! -f "$TEST_ROOT/expected" ]]
# second time succeeds
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
  (( "$badDiff" == 0 ))
)
