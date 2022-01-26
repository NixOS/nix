source common.sh

STORE_INFO=$(nix store ping 2>&1)

echo "$STORE_INFO" | grep "Store URL: ${NIX_REMOTE}"

if [[ -v NIX_DAEMON_PACKAGE ]] && isDaemonNewer "2.7.0pre20220126"; then
    DAEMON_VERSION=$($NIX_DAEMON_PACKAGE/bin/nix-daemon --version | cut -d' ' -f3)
    echo "$STORE_INFO" | grep "Version: $DAEMON_VERSION"
fi

expect 127 NIX_REMOTE=unix:$PWD/store nix store ping || \
    fail "nix store ping on a non-existent store should fail"
