# shellcheck shell=bash

badTestNames=()

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
    if ! diff >&2 --color=always --unified "$expectedOrEmpty" "$got"; then
        echo >&2 "FAIL: evaluation result of $testName not as expected"
        # shellcheck disable=SC2034
        badDiff=1
        badTestNames+=("$testName")
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
            echo >&2 'Output did mot match, but accepted output as the persisted expected output.'
            echo >&2 'That means the next time the tests are run, they should pass.'
            set -x
        else
            set +x
            echo >&2 'NOTE: Environment variable _NIX_TEST_ACCEPT is defined,'
            echo >&2 'indicating the unexpected output should be accepted as the expected output going forward,'
            echo >&2 'but no tests had unexpected output so there was no expected output to update.'
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
            echo >&2 ''
            echo >&2 'The following tests had unexpected output:'
            for testName in "${badTestNames[@]}"; do
                echo >&2 "    $testName"
            done
            echo >&2 ''
            echo >&2 'You can rerun this test with:'
            echo >&2 ''
            echo >&2 "    _NIX_TEST_ACCEPT=1 meson test --suite ${TEST_SUITE_NAME} ${TEST_NAME}"
            echo >&2 ''
            echo >&2 'to regenerate the files containing the expected output,'
            echo >&2 'and then view the git diff to decide whether a change is'
            echo >&2 'good/intentional or bad/unintentional.'
            echo >&2 'If the diff contains arbitrary or impure information,'
            echo >&2 'please improve the normalization that the test applies to the output.'
            set -x
        fi
        exit $(( "$badExitCode" + "$badDiff" ))
    fi
}
