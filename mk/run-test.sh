#!/usr/bin/env bash

set -u

red=""
green=""
yellow=""
normal=""

test=$1

dir="$(dirname "${BASH_SOURCE[0]}")"
source "$dir/common-test.sh"

post_run_msg="ran test $test..."
if [ -t 1 ]; then
    red="[31;1m"
    green="[32;1m"
    yellow="[33;1m"
    normal="[m"
fi

run_test () {
    (init_test 2>/dev/null > /dev/null)
    log="$(run_test_proper 2>&1)"
    status=$?
}

run_test

# Hack: Retry the test if it fails with “unexpected EOF reading a line” as these
# appear randomly without anyone knowing why.
# See https://github.com/NixOS/nix/issues/3605 for more info
if [[ $status -ne 0 && $status -ne 99 && \
    "$(uname)" == "Darwin" && \
    "$log" =~ "unexpected EOF reading a line" \
]]; then
    echo "$post_run_msg [${yellow}FAIL$normal] (possibly flaky, so will be retried)"
    echo "$log" | sed 's/^/    /'
    run_test
fi

if [ $status -eq 0 ]; then
  echo "$post_run_msg [${green}PASS$normal]"
elif [ $status -eq 99 ]; then
  echo "$post_run_msg [${yellow}SKIP$normal]"
else
  echo "$post_run_msg [${red}FAIL$normal]"
  echo "$log" | sed 's/^/    /'
  exit "$status"
fi
