source ../common.sh

clearStore
clearCache

# Need backend to support git-hashing too
requireDaemonNewerThan "2.18.0pre20230908"

enableFeatures "git-hashing"

restartDaemon
