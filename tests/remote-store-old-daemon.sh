# Test that the new Nix can properly talk to an old daemon.
# If `$OUTER_NIX` isn't set (e.g. when bootsraping), just skip this test

if [[ -n "$OUTER_NIX" ]]; then
    export NIX_DAEMON_BIN=$OUTER_NIX/bin/nix-daemon
    source remote-store.sh
fi
