#!/usr/bin/env bash

set -eu -o pipefail

test=$1

dir="$(dirname "${BASH_SOURCE[0]}")"
source "$dir/common-test.sh"

(init_test)
run_test_proper
