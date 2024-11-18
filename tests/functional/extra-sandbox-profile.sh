#!/usr/bin/env bash

source common.sh

if [[ $(uname) != Darwin ]]; then skipTest "Need Darwin"; fi

DEST_FILE="${TEST_ROOT}/foo"

testSandboxProfile () (
    set -e

    sandboxMode="$1"

    rm -f "${DEST_FILE}"
    nix-build --no-out-link ./extra-sandbox-profile.nix \
        --option sandbox "$sandboxMode" \
        --argstr seed "$RANDOM" \
        --argstr destFile "${DEST_FILE}"

    ls -l "${DEST_FILE}"
)

testSandboxProfile "false"
expectStderr 2 testSandboxProfile "true"
testSandboxProfile "relaxed"
