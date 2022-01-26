source common.sh

STORE_INFO=$(nix store ping 2>&1)

echo "$STORE_INFO" | grep "Store URL: ${NIX_REMOTE}"

if isDaemonNewer "2.7pre20220126"; then
    echo "$STORE_INFO" | grep "Version: $($NIX_DAEMON_PACKAGE/bin/nix-daemon --version)"
fi

expect 127 NIX_REMOTE=unix:$PWD/store nix store ping || \
    fail "nix store ping on a non-existent store should fail"
