#!/usr/bin/env bash

source common.sh

[[ $(uname) == Darwin ]] || skipTest "Need Darwin"
needLocalStore "registers fixture paths in the local test store"
[[ ${_NIX_TEST_NO_SANDBOX:-0} != 1 ]] || skipTest "Sandbox initialization is disabled"

# The sandbox allows access to TMPDIR, so keep it outside the fixture store.
export TMPDIR="$TEST_ROOT/tmp"
mkdir -p "$TMPDIR"

prepareTestInputs() {
    local testCase="$1" path kind

    # Use the same paths as the build, but create them ourselves.
    nix-instantiate --eval --strict --json ./darwin-sandbox.nix -A "$testCase.fixtures" \
        --argstr storeDir "$NIX_STORE_DIR" > "$TEST_ROOT/fixtures.json"

    while IFS=$'\t' read -r path kind; do
        if [[ $kind == directory ]]; then
            mkdir "$path"
            printf 'fixture\n' > "$path/data"
        else
            printf 'fixture\n' > "$path"
        fi

        # This file has the same hash in its name, but is not a declared input.
        # The sandbox must block access to it even though it exists.
        printf 'denied\n' > "$path-denied"
    done < <(jq -r '.[] | [.path, .kind] | @tsv' "$TEST_ROOT/fixtures.json")

    # Tell Nix these inputs already exist, without building each one.
    jq -r '.[] | .path, "", "0"' "$TEST_ROOT/fixtures.json" > "$TEST_ROOT/registration"
    nix-store --register-validity < "$TEST_ROOT/registration"
}

runTest() {
    local testCase="$1" output

    clearStore
    prepareTestInputs "$testCase"

    # The shell and its libraries need access to the real /nix/store.
    # Our test inputs live in a separate store under TEST_ROOT.
    output=$(nix-build ./darwin-sandbox.nix -A "$testCase.test" --no-out-link \
        --argstr storeDir "$NIX_STORE_DIR" \
        --option sandbox true \
        --option sandbox-paths /nix/store)

    [[ $(cat "$output") == success ]]
}

runTest ordering
runTest capacity
