source common.sh

export NIX_DAEMON_SOCKET_PATH=$NIX_STATE_DIR/daemon-socket/socket
startDaemon

tmpFile="$(mktemp)"
echo fnord > "$tmpFile"

prefetch() {
    bash -c "exec -a nix-prefetch-url nix-prefetch-url-2.3 $@"
}

p="$(prefetch file://"$tmpFile" 2>&1 | tail -n2 | head -n1 | awk '{ print $3 }' | sed -e "s/'//g")"

test 6 = "$(stat -c '%s' "$p")"
