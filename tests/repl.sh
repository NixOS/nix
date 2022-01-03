source common.sh

replCmds="
simple = 1
simple = import ./simple.nix
:b simple
:log simple
"

replFailingCmds="
failing = import ./simple-failing.nix
:b failing
:log failing
"

replUndefinedVariable="
import ./undefined-variable.nix
"

testRepl () {
    local nixArgs=("$@")
    local replOutput="$(nix repl "${nixArgs[@]}" <<< "$replCmds")"
    echo "$replOutput"
    local outPath=$(echo "$replOutput" |&
        grep -o -E "$NIX_STORE_DIR/\w*-simple")
    nix path-info "${nixArgs[@]}" "$outPath"
    # simple.nix prints a PATH during build
    echo "$replOutput" | grep -qs 'PATH=' || fail "nix repl :log doesn't output logs"
    local replOutput="$(nix repl "${nixArgs[@]}" <<< "$replFailingCmds" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grep -qs 'This should fail' \
      || fail "nix repl :log doesn't output logs for a failed derivation"
    local replOutput="$(nix repl --show-trace "${nixArgs[@]}" <<< "$replUndefinedVariable" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grep -qs "while evaluating the file" \
      || fail "nix repl --show-trace doesn't show the trace"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/store?real=$NIX_STORE_DIR"
