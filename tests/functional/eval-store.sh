source common.sh

# Using `--eval-store` with the daemon will eventually copy everything
# to the build store, invalidating most of the tests here
needLocalStore "“--eval-store” doesn't achieve much with the daemon"

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

clearStore
rm -rf "$eval_store"

# Confirm that import-from-derivation builds on the build store
[[ $(nix eval --eval-store "$eval_store?require-sigs=false" --impure --raw --file ./ifd.nix) = hi ]]
ls $NIX_STORE_DIR/*dependencies-top/foobar
(! ls $eval_store/nix/store/*dependencies-top/foobar)
