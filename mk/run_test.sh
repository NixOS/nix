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
(cd tests && env ${TESTS_ENVIRONMENT} init.sh 2>/dev/null > /dev/null)
log="$(cd $(dirname $1) && env ${TESTS_ENVIRONMENT} $(basename $1) 2>&1)"
status=$?
if [ $status -eq 0 ]; then
  echo "$post_run_msg [${green}PASS$normal]"
elif [ $status -eq 99 ]; then
  echo "$post_run_msg [${yellow}SKIP$normal]"
else
  echo "$post_run_msg [${red}FAIL$normal]"
  echo "$log" | sed 's/^/    /'
  exit "$status"
fi
