source ../common.sh

TODO_NixOS

clearStore
clearCache

# Need backend to support git-hashing too
requireDaemonNewerThan "2.19"

enableFeatures "git-hashing"

restartDaemon
