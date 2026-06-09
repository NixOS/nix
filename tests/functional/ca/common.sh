# shellcheck shell=bash
source ../common.sh

# Need backend to support revamped CA
requireDaemonNewerThan "2.35.0pre20260303"

enableFeatures "ca-derivations"

TODO_NixOS

restartDaemon
