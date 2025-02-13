# shellcheck shell=bash
source ../common.sh

# Need backend to support revamped CA
requireDaemonNewerThan "2.34.0pre20251217"

enableFeatures "ca-derivations"

TODO_NixOS

restartDaemon
