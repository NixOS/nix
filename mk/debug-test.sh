#!/usr/bin/env bash

set -eu -o pipefail

test=$1
init=${2-}

dir="$(dirname "${BASH_SOURCE[0]}")"
source "$dir/common-test.sh"

if [ -n "$init" ]; then
    (init_test)
fi
run_test_proper
