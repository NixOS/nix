source ../common.sh

enableFeatures "external-gc-daemon"
echo "gc-socket-path = $NIX_GC_SOCKET_PATH" >> "$NIX_CONF_DIR"/nix.conf

startGcDaemon
