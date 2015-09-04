# Golden test support
#
# Test that the output of the given test matches what is expected. If
# `_NIX_TEST_ACCEPT` is non-empty also update the expected output so
# that next time the test succeeds.
function diffAndAcceptInner() {
    local -r testName=$1
    local -r got="$2"
    local -r expected="$3"

    # Absence of expected file indicates empty output expected.
    if test -e "$expected"; then
        local -r expectedOrEmpty="$expected"
    else
        local -r expectedOrEmpty=lang/empty.exp
    fi

    # Diff so we get a nice message
    if ! diff "$got" "$expectedOrEmpty"; then
        echo "FAIL: evaluation result of $testName not as expected"
        badDiff=1
    fi

    # Update expected if `_NIX_TEST_ACCEPT` is non-empty.
    if test -n "${_NIX_TEST_ACCEPT-}"; then
        cp "$got" "$expected"
        # Delete empty expected files to avoid bloating the repo with
        # empty files.
        if ! test -s "$expected"; then
            rm "$expected"
        fi
    fi
}
