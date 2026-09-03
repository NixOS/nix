# shellcheck shell=bash
# shellcheck source=/dev/null
# shellcheck disable=SC2034

source "$_NIX_TEST_SOURCE_DIR"/common/characterisation/framework.sh

testDir=$PWD

# TODO: move init to characterisation/framework.sh
badDiff=0
badExitCode=0

testCases=( "$testDir"/cases/*.in )

for test in "${testCases[@]}"; do
    test="$(basename "$test" .in)"
    in="$testDir/cases/$test.in"
    actual="$TEST_ROOT/$test.actual"
    expected="$testDir/cases/$test.expected"
    declare -a flags=()
    if test -e "$testDir/cases/$test.flags"; then
      read -r -a flags < "$testDir/cases/$test.flags"
    fi

    # Allow putting comments (lines starting with `# COM:`) in the test for
    # documentation purposes. Regular comments are not skipped, since those are
    # also interpreted by the repl.
    inputWithoutComments=$(grep -Ev '^[[:space:]]*#[[:space:]]*COM:' "$in")

    if [ -f "$testDir/cases/$test.in.debugexpr.nix" ]; then
        if [[ "$test" =~ debugger-fail-.* ]]; then
            expectedFail=1
        elif [[ "$test" =~ debugger-okay-.* ]]; then
            expectedFail=0
        else
            die "unexpected debugger test name: '$test', should be either debugger-okay-* or debugger-fail-*"
        fi

        set +e
        (cd "$testDir/cases"; set +x; echo "$inputWithoutComments" | runDebugRepl "$testDir/cases/$test.in.debugexpr.nix" "$testDir" "${flags[@]}" 2>&1) > "$actual"
        debuggerTestCode=$?
        set -e

        if ((expectedFail == 0 && debuggerTestCode != 0)); then
            echo "test failed: $test (exit code $debuggerTestCode)" >&2
            badExitCode=1
        elif ((expectedFail == 1 && debuggerTestCode == 0)); then
            echo "test failed: $test unexpectedly succeeded" >&2
            badExitCode=1
        fi
    else
        (cd "$testDir/cases"; set +x; echo "$inputWithoutComments" | runRepl "$testDir" "${flags[@]}" 2>&1) > "$actual" || {
            echo "test failed: $test (exit code $?)" >&2
            badExitCode=1
        }
    fi

    diffAndAcceptInner "$test" "$actual" "$expected"
done

characterisationTestExit
