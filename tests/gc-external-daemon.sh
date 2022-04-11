source common.sh

sed -i 's/experimental-features .*/& external-gc-daemon/' "$NIX_CONF_DIR"/nix.conf

export NIX_GC_SOCKET_PATH=$TEST_ROOT/gc.socket
startGcDaemon() {
    # Start the daemon, wait for the socket to appear.  !!!
    # ‘nix-daemon’ should have an option to fork into the background.
    rm -f $NIX_GC_SOCKET_PATH
    $(dirname $(command -v nix))/../libexec/nix/nix-find-roots \
        -l "$NIX_GC_SOCKET_PATH" \
        -d "$NIX_STATE_DIR" \
        -s "$NIX_STORE_DIR" \
            &
    for ((i = 0; i < 30; i++)); do
        if [[ -S $NIX_GC_SOCKET_PATH ]]; then break; fi
        sleep 1
    done
    pidGcDaemon=$!
    trap "killGcDaemon" EXIT
}

killGcDaemon() {
    kill $pidGcDaemon
    for i in {0.10}; do
        kill -0 $pidGcDaemon || break
        sleep 1
    done
    kill -9 $pidGcDaemon || true
    wait $pidGcDaemon || true
    trap "" EXIT
}

startGcDaemon

bash ./gc.sh
bash ./gc-concurrent.sh
bash ./gc-runtime.sh
bash ./gc-auto.sh
