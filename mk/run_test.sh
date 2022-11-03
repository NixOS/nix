#!/bin/sh

set -u

red=""
green=""
yellow=""
normal=""

post_run_msg="ran test $1..."
if [ -t 1 ]; then
    red="[31;1m"
    green="[32;1m"
    yellow="[33;1m"
    normal="[m"
fi

run_test () {
    (cd tests && env ${TESTS_ENVIRONMENT} init.sh 2>/dev/null > /dev/null)
    log="$(cd $(dirname $1) && env ${TESTS_ENVIRONMENT} $(basename $1) 2>&1)"
    status=$?
}

run_test "$1"

# Hack: Retry the test if it fails with “unexpected EOF reading a line” as these
# appear randomly without anyone knowing why.
# See https://github.com/NixOS/nix/issues/3605 for more info
if [[ $status -ne 0 && $status -ne 99 && \
    "$(uname)" == "Darwin" && \
    "$log" =~ "unexpected EOF reading a line" \
]]; then
    echo "$post_run_msg [${yellow}FAIL$normal] (possibly flaky, so will be retried)"
    echo "$log" | sed 's/^/    /'
    run_test "$1"
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
