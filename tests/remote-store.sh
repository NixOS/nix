export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

startDaemon

storeCleared=1 $SHELL $NIX_TEST_ROOT/user-envs.sh

nix-store --dump-db > $TEST_ROOT/d1
NIX_REMOTE= nix-store --dump-db > $TEST_ROOT/d2
cmp $TEST_ROOT/d1 $TEST_ROOT/d2

nix-store --gc --max-freed 1K

killDaemon
