#!/usr/bin/env bash

set -eu -o pipefail

red=""
green=""
yellow=""
normal=""

test=$1
init=${2-}

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
    if [ -n "$init" ]; then
        (init_test 2>/dev/null > /dev/null)
    fi
    log="$(run_test_proper 2>&1)" && status=0 || status=$?
}

run_test

if [ $status -eq 0 ]; then
  echo "$post_run_msg [${green}PASS$normal]"
elif [ $status -eq 99 ]; then
  echo "$post_run_msg [${yellow}SKIP$normal]"
else
  echo "$post_run_msg [${red}FAIL$normal]"
  echo "$log" | sed 's/^/    /'
  exit "$status"
fi
