# Test the functions for testing themselves!
# Also test some assumptions on how bash works that they rely on.
source common.sh

# `true` should exit with 0
expect 0 true

# `false` should exit with 1
expect 1 false

# `expect` will fail when we get it wrong
expect 1 expect 0 false

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

pipefailure () {
    # shellcheck disable=SC2216
    true | false | true
}
expect 1 pipefailure
unset pipefailure

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
funBang () {
    ! true
}
expect 1 funBang
unset funBang

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
