source ../common.sh

enableFeatures "ca-derivations"

requireDaemonVersionAtleast "2.4pre"

restartDaemon
