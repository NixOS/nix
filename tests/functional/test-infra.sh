#!/usr/bin/env bash

# Test the functions for testing themselves!
# Also test some assumptions on how bash works that they rely on.
source common.sh

# `true` should exit with 0
expect 0 true

# `false` should exit with 1
expect 1 false

# `expect` will fail when we get it wrong
expect 1 expect 0 false

function ret() {
  return "$1"
}

# `expect` can call functions, not just executables
expect 0 ret 0
expect 1 ret 1

# `expect` supports negative exit codes
expect -1 ret -1

# or high positive ones, equivalent to negative ones
expect 255 ret 255
expect 255 ret -1
expect -1 ret 255

# but it doesn't confuse negative exit codes with positive ones
expect 1 expect -10 ret 10

noisyTrue () {
    echo YAY! >&2
    true
}

noisyFalse () {
    echo NAY! >&2
    false
}

# These should redirect standard error to standard output
expectStderr 0 noisyTrue | grepQuiet YAY
expectStderr 1 noisyFalse | grepQuiet NAY

# `set -o pipefile` is enabled

# shellcheck disable=SC2317# shellcheck disable=SC2317
pipefailure () {
    # shellcheck disable=SC2216
    true | false | true
}
expect 1 pipefailure
unset pipefailure

# shellcheck disable=SC2317
pipefailure () {
    # shellcheck disable=SC2216
    false | true | true
}
expect 1 pipefailure
unset pipefailure

commandSubstitutionPipeFailure () {
    # shellcheck disable=SC2216
    res=$(set -eu -o pipefail; false | true | echo 0)
}
expect 1 commandSubstitutionPipeFailure

# `set -u` is enabled

# note (...), making function use subshell, as unbound variable errors
# in the outer shell are *rightly* not recoverable.
useUnbound () (
    set -eu
    # shellcheck disable=SC2154
    echo "$thisVariableIsNotBound"
)
expect 1 useUnbound

# ! alone unfortunately negates `set -e`, but it works in functions:
# shellcheck disable=SC2251
! true
# shellcheck disable=SC2317
funBang () {
    ! true
}
expect 1 funBang
unset funBang

# callerPrefix can be used by the test framework to improve error messages
# it reports about our call site here
echo "<[$(callerPrefix)]>" | grepQuiet -F "<[test-infra.sh:$LINENO: ]>"

# `grep -v -q` is not what we want for exit codes, but `grepInverse` is
# Avoid `grep -v -q`. The following line proves the point, and if it fails,
# we'll know that `grep` had a breaking change or `-v -q` may not be portable.
{ echo foo; echo bar; } | grep -v -q foo
{ echo foo; echo bar; } | expect 1 grepInverse foo

# `grepQuiet` is quiet
res=$(set -eu -o pipefail; echo foo | grepQuiet foo | wc -c)
(( res == 0 ))
unset res

# `greqQietInverse` is both
{ echo foo; echo bar; } | expect 1 grepQuietInverse foo
res=$(set -eu -o pipefail; echo foo | expect 1 grepQuietInverse foo | wc -c)
(( res == 0 ))
unset res

# `grepQuiet` does not allow newlines in its arguments, because grep quietly
# treats them as multiple queries.
{ echo foo; echo bar; } | expectStderr -101 grepQuiet $'foo\nbar' \
  | grepQuiet -E 'test-infra\.sh:[0-9]+: in call to grepQuiet: newline not allowed in arguments; grep would try each line individually as if connected by an OR operator'

# We took the blue pill and woke up in a world where `grep` is moderately safe.
expectStderr -101 grep $'foo\nbar' \
  | grepQuiet -E 'test-infra\.sh:[0-9]+: in call to grep: newline not allowed in arguments; grep would try each line individually as if connected by an OR operator'
