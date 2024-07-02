# shellcheck shell=bash

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
        local -r expectedOrEmpty=characterisation/empty
    fi

    # Diff so we get a nice message
    if ! diff --color=always --unified "$expectedOrEmpty" "$got"; then
        echo "FAIL: evaluation result of $testName not as expected"
        # shellcheck disable=SC2034
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

function characterisationTestExit() {
    # Make sure shellcheck knows all these will be defined by the caller
    : "${badDiff?} ${badExitCode?}"

    if test -n "${_NIX_TEST_ACCEPT-}"; then
        if (( "$badDiff" )); then
            set +x
            echo 'Output did mot match, but accepted output as the persisted expected output.'
            echo 'That means the next time the tests are run, they should pass.'
            set -x
        else
            set +x
            echo 'NOTE: Environment variable _NIX_TEST_ACCEPT is defined,'
            echo 'indicating the unexpected output should be accepted as the expected output going forward,'
            echo 'but no tests had unexpected output so there was no expected output to update.'
            set -x
        fi
        if (( "$badExitCode" )); then
            exit "$badExitCode"
        else
            skipTest "regenerating golden masters"
        fi
    else
        if (( "$badDiff" )); then
            set +x
            echo ''
            echo 'You can rerun this test with:'
            echo ''
            echo "    _NIX_TEST_ACCEPT=1 make tests/functional/${TEST_NAME}.sh.test"
            echo ''
            echo 'to regenerate the files containing the expected output,'
            echo 'and then view the git diff to decide whether a change is'
            echo 'good/intentional or bad/unintentional.'
            echo 'If the diff contains arbitrary or impure information,'
            echo 'please improve the normalization that the test applies to the output.'
            set -x
        fi
        exit $(( "$badExitCode" + "$badDiff" ))
    fi
}
