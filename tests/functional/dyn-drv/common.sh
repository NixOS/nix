# shellcheck shell=bash
source ../common.sh

# Need backend to support text-hashing too
requireDaemonNewerThan "2.16.0pre20230419"

enableFeatures "ca-derivations dynamic-derivations"

TODO_NixOS

restartDaemon
