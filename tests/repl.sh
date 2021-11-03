source common.sh

replCmds="
simple = import ./simple.nix
:b simple
"

testRepl () {
    local nixArgs=("$@")
    local outPath=$(nix repl "${nixArgs[@]}" <<< "$replCmds" |&
        grep -o -E "$NIX_STORE_DIR/\w*-simple")
    nix path-info "${nixArgs[@]}" "$outPath"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/store?real=$NIX_STORE_DIR"
