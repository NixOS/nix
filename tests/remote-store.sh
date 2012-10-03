source common.sh

clearStore
clearManifests
startDaemon
$SHELL ./user-envs.sh
killDaemon
