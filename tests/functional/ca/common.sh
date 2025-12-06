# shellcheck shell=bash
source ../common.sh

# Need backend to support revamped CA
requireDaemonNewerThan "2.33.0pre20251121"

enableFeatures "ca-derivations"

TODO_NixOS

restartDaemon
