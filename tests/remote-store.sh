source common.sh

clearStore

startDaemon

storeCleared=1 $SHELL ./user-envs.sh

nix-store --dump-db > $TEST_ROOT/d1
NIX_REMOTE= nix-store --dump-db > $TEST_ROOT/d2
cmp $TEST_ROOT/d1 $TEST_ROOT/d2

nix-store --gc --max-freed 1K

killDaemon

user=$(whoami)
[ -e $NIX_STATE_DIR/gcroots/per-user/$user ]
[ -e $NIX_STATE_DIR/profiles/per-user/$user ]
