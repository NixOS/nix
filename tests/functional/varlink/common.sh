# shellcheck shell=bash
source ../common.sh

enableFeatures "builder-rpc ca-derivations dynamic-derivations"

TODO_NixOS

restartDaemon
