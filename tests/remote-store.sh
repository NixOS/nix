source common.sh

clearStore

# Ensure "fake ssh" remote store works just as legacy fake ssh would.
nix --store ssh-ng://localhost?remote-store=$TEST_ROOT/other-store doctor

# Ensure that store ping trusted works with ssh-ng://
nix --store ssh-ng://localhost?remote-store=$TEST_ROOT/other-store store ping --json | jq -e '.trusted'

startDaemon

if isDaemonNewer "2.15pre0"; then
    # Ensure that ping works trusted with new daemon
    nix store ping --json | jq -e '.trusted'
else
    # And the the field is absent with the old daemon
    nix store ping --json | jq -e 'has("trusted") | not'
fi

# Test import-from-derivation through the daemon.
[[ $(nix eval --option "log-import-from-derivation" true --impure --raw ./import-from-derivation.nix) = hi ]]

storeCleared=1 NIX_REMOTE_=$NIX_REMOTE $SHELL ./user-envs.sh

nix-store --gc --max-freed 1K

nix-store --dump-db > $TEST_ROOT/d1
NIX_REMOTE= nix-store --dump-db > $TEST_ROOT/d2
cmp $TEST_ROOT/d1 $TEST_ROOT/d2

killDaemon
