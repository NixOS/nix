#!/usr/bin/env bash

source common/test-root.sh
source common/paths.sh

set -eu -o pipefail

source characterisation/framework.sh

badDiff=0
badExitCode=0

store="$TEST_ROOT/store"

for nixFile in derivation/*.nix; do
    drvPath=$(env -u NIX_STORE nix-instantiate --store "$store" --pure-eval --expr "$(< "$nixFile")")
    testName=$(basename "$nixFile" .nix)
    got="${store}${drvPath}"
    expected="derivation/$testName.drv"
    diffAndAcceptInner "$testName" "$got" "$expected"
done

characterisationTestExit
