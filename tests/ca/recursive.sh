#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.4pre20210623"

export NIX_TESTS_CA_BY_DEFAULT=1
cd ..
source ./recursive.sh


