source common.sh

# Using `--eval-store` with the daemon will eventually copy everything
# to the build store, invalidating most of the tests here
needLocalStore

eval_store=$TEST_ROOT/eval-store

clearStore
rm -rf "$eval_store"

nix build -f dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
(! ls $NIX_STORE_DIR/*.drv)
ls $eval_store/nix/store/*.drv

clearStore
rm -rf "$eval_store"

nix-instantiate dependencies.nix --eval-store "$eval_store"
(! ls $NIX_STORE_DIR/*.drv)
ls $eval_store/nix/store/*.drv

clearStore
rm -rf "$eval_store"

nix-build dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
(! ls $NIX_STORE_DIR/*.drv)
ls $eval_store/nix/store/*.drv
