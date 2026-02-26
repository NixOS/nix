#!/usr/bin/env bash

# Characterization tests for nix-env, nix-build, and nix-instantiate.
# Captures error messages, output formats, and edge-case behavior.
#
# Expected output files in: cli-characterisation/
# Regenerate with: _NIX_TEST_ACCEPT=1 meson test -C build cli-characterisation

source common.sh

source characterisation/framework.sh

set +x

badDiff=0
badExitCode=0

# Normalize store paths, hashes, source paths, and system
normalize() {
    sed -i \
        -e "s|${NIX_STORE_DIR:-/nix/store}/[a-z0-9]\{32\}|/test-root/store/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx|g" \
        -e "s|$TEST_ROOT|/test-root|g" \
        -e "s|$(pwd)|/pwd|g" \
        -e "s|$system|SYSTEM|g" \
        -e "s|/nix/store/[a-z0-9]\{32\}|/nix/store/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx|g" \
        -e "s|/test-root/store/[a-z0-9]\{32\}|/test-root/store/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx|g" \
        -e "s|'[a-z0-9]\{32\}-|'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-|g" \
        "$@"
}

diffAndAccept() {
    local testName="$1"
    local got="cli-characterisation/$testName.$2"
    local expected="cli-characterisation/$testName.$3"
    diffAndAcceptInner "$testName" "$got" "$expected"
}

clearProfiles

# Each test case has a descriptor: cli-characterisation/<name>.cmd
# containing: <expected-exit-code> <command...>
#
# stdout -> <name>.out, stderr -> <name>.err
# Both are compared against <name>.out.exp / <name>.err.exp

for cmdFile in cli-characterisation/*.cmd; do
    testName=$(basename "$cmdFile" .cmd)
    echo "testing $testName"

    # read returns 1 on EOF without trailing newline; handle it
    read -r expectedExit cmd < "$cmdFile" || [[ -n "$expectedExit" ]]

    if
        # shellcheck disable=SC2086 # word splitting of cmd is intended
        expect "$expectedExit" $cmd \
            1> "cli-characterisation/$testName.out" \
            2> "cli-characterisation/$testName.err"
    then
        normalize "cli-characterisation/$testName.out" "cli-characterisation/$testName.err"
        diffAndAccept "$testName" out out.exp
        diffAndAccept "$testName" err err.exp
    else
        echo "FAIL: $testName exited with wrong code"
        badExitCode=1
    fi
done

# --- Multi-step tests that need a real .drv path in the store ---

drvPath=$(nix-instantiate ./cli-characterisation/simple.nix)

# Constructor: "building more than one derivation output is not supported"
testName=nix-build-multi-output
echo "testing $testName"
if
    expect 1 nix-build "$drvPath!out,bin" \
        1> "cli-characterisation/$testName.out" \
        2> "cli-characterisation/$testName.err"
then
    normalize "cli-characterisation/$testName.out" "cli-characterisation/$testName.err"
    diffAndAccept "$testName" out out.exp
    diffAndAccept "$testName" err err.exp
else
    echo "FAIL: $testName exited with wrong code"
    badExitCode=1
fi

# Constructor: "derivation does not have output"
testName=nix-build-bad-output
echo "testing $testName"
if
    expect 1 nix-build "$drvPath!nonexistent" \
        1> "cli-characterisation/$testName.out" \
        2> "cli-characterisation/$testName.err"
then
    normalize "cli-characterisation/$testName.out" "cli-characterisation/$testName.err"
    diffAndAccept "$testName" out out.exp
    diffAndAccept "$testName" err err.exp
else
    echo "FAIL: $testName exited with wrong code"
    badExitCode=1
fi

characterisationTestExit
