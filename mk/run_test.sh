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

start_time=$(date -u +%s)
log="$(cd $(dirname $1) && env ${TESTS_ENVIRONMENT} $(basename $1) 2>&1)"
stop_time=$(date -u +%s)
elapsed_time=$(($stop_time-$start_time))

if [ $status -eq 0 ]; then
  echo "$post_run_msg [${green}PASS$normal] in ${elapsed_time}s"
elif [ $status -eq 99 ]; then
  echo "$post_run_msg [${yellow}SKIP$normal] after ${elapsed_time}s"
else
  echo "$post_run_msg [${red}FAIL$normal] in ${elapsed_time}s"
  echo "$log" | sed 's/^/    /'
  exit "$status"
fi
