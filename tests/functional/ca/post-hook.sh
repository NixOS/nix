#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.4pre20210626"

export NIX_TESTS_CA_BY_DEFAULT=1
cd ..
# shellcheck source=/dev/null
source ./post-hook.sh


