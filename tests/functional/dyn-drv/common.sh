# shellcheck shell=bash
source ../common.sh

# Need daemon to support the `realisation-with-path-not-hash` protocol
# feature (build trace rework). Without it, queryRealisation returns
# nullptr and CA/dynamic derivation output lookup fails.
requireDaemonNewerThan "2.35.0pre20260303"

enableFeatures "ca-derivations dynamic-derivations"

TODO_NixOS

restartDaemon
