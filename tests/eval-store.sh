source common.sh

enableFeatures nix-command

# Using `--eval-store` with the daemon will eventually copy everything
# to the build store, invalidating most of the tests here
needLocalStore

eval_store=$TEST_ROOT/eval-store

resetEvalStore() {
    chmod -R +w "$eval_store" || true
    rm -rf "$eval_store"

    # Should Nix try to substitute this automatically into the eval store?
    if isTestOnSystemNix; then
        nix copy --no-check-sigs --to "$eval_store" "$SHELL" "$coreutils"
    fi
}
clearStore

resetEvalStore

nix build -f dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
(! ls $NIX_STORE_DIR/*.drv) || ignoreSharedStore
ls $eval_store/nix/store/*.drv

clearStore
resetEvalStore

nix-instantiate dependencies.nix --eval-store "$eval_store"
(! ls $NIX_STORE_DIR/*.drv) || ignoreSharedStore
ls $eval_store/nix/store/*.drv

clearStore
resetEvalStore

nix-build dependencies.nix --eval-store "$eval_store" -o "$TEST_ROOT/result"
[[ -e $TEST_ROOT/result/foobar ]]
(! ls $NIX_STORE_DIR/*.drv) || ignoreSharedStore
ls $eval_store/nix/store/*.drv
