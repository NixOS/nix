# shellcheck shell=bash

source ../common.sh

TODO_NixOS # Need to enable git hashing feature and make sure test is ok for store we don't clear

clearStore
clearCache

# Need backend to support git-hashing too
requireDaemonNewerThan "2.19"

enableFeatures "git-hashing"

restartDaemon
