source common.sh

clearStore

# Ensure "fake ssh" remote store works just as legacy fake ssh would.
nix --store ssh-ng://localhost?remote-store=$TEST_ROOT/other-store doctor

startDaemon

storeCleared=1 NIX_REMOTE_=$NIX_REMOTE $SHELL ./user-envs.sh

nix-store --dump-db > $TEST_ROOT/d1
NIX_REMOTE= nix-store --dump-db > $TEST_ROOT/d2
cmp $TEST_ROOT/d1 $TEST_ROOT/d2

nix-store --gc --max-freed 1K

killDaemon

user=$(whoami)
[ -e $NIX_STATE_DIR/gcroots/per-user/$user ]
[ -e $NIX_STATE_DIR/profiles/per-user/$user ]
