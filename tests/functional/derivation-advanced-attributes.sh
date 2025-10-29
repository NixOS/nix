#!/usr/bin/env bash

source common/test-root.sh
source common/paths.sh

set -eu -o pipefail

source characterisation/framework.sh

badDiff=0
badExitCode=0

store="$TEST_ROOT/store"

if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    drvDir=ia
    flags=(--arg contentAddress false)
else
    drvDir=ca
    flags=(--arg contentAddress true --extra-experimental-features ca-derivations)
fi

for nixFile in derivation/*.nix; do
    drvPath=$(env -u NIX_STORE nix-instantiate --store "$store" --pure-eval "${flags[@]}" --expr "$(< "$nixFile")")
    testName=$(basename "$nixFile" .nix)
    got="${store}${drvPath}"
    expected="derivation/${drvDir}/${testName}.drv"
    diffAndAcceptInner "$testName" "$got" "$expected"
done

characterisationTestExit
