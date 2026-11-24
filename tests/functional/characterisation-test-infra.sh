#!/usr/bin/env bash

# Test the function for lang.sh
source common.sh

source common/characterisation/framework.sh

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

# matches empty, non-existent file is the same as empty file
echo -n > "$TEST_ROOT/got"
(
  diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/does-not-exist"
  (( "$badDiff" == 0 ))
)

# doesn't matches non-empty, non-existent file is the same as empty file
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

# _NIX_TEST_ACCEPT=0 means off: doesn't match, `badDiff=1` set, file unchanged
echo Hi! > "$TEST_ROOT/got"
echo Bye! > "$TEST_ROOT/expected"
(
  _NIX_TEST_ACCEPT=0 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
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

# _NIX_TEST_ACCEPT matches empty, non-existent file not created
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

# doesn't match, the reporter receives the diff of the expected and the actual output.
# Fixed modification times and time zone make the diff's header lines reproducible.
echo Hi! > "$TEST_ROOT/got"
echo Bye! > "$TEST_ROOT/expected"
touch -d @0 "$TEST_ROOT/got" "$TEST_ROOT/expected"
expectedDiff=$(printf '%s\n' \
  $'\e[1m--- '"$TEST_ROOT/expected"$'\t1970-01-01 00:00:00.000000000 +0000\e[0m' \
  $'\e[1m+++ '"$TEST_ROOT/got"$'\t1970-01-01 00:00:00.000000000 +0000\e[0m' \
  $'\e[36m@@ -1 +1 @@\e[0m' \
  $'\e[31m-Bye!\e[0m' \
  $'\e[32m+Hi!\e[0m')

# `reportDiffAcceptFailure` gets the test name, then the diff
(
  function reportDiffAcceptFailure() {
    printf '%s' "$1" > "$TEST_ROOT/reported-1"
    printf '%s' "$2" > "$TEST_ROOT/reported-2"
  }
  TZ=UTC0 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected"
)
[[ "$(< "$TEST_ROOT/reported-1")" == test ]]
[[ "$(< "$TEST_ROOT/reported-2")" == "$expectedDiff" ]]

# the default `reportDiffAcceptFailure` prints the diff, then a line that names the test
(
  # xtrace would write into the captured stderr
  { set +x; } 2>/dev/null
  TZ=UTC0 diffAndAcceptInner test "$TEST_ROOT/got" "$TEST_ROOT/expected" 2> "$TEST_ROOT/stderr"
)
diff -u \
  <(printf '%s\n' "$expectedDiff" "subtest 'test' failed: characterised output of test not as expected") \
  "$TEST_ROOT/stderr"
