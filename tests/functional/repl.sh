#!/usr/bin/env bash

source common.sh

testDir="$PWD"
cd "$TEST_ROOT"

replCmds="
simple = 1
simple = import $testDir/simple.nix
:bl simple
:log simple
"

replFailingCmds="
failing = import $testDir/simple-failing.nix
:b failing
:log failing
"

replUndefinedVariable="
import $testDir/undefined-variable.nix
"

TODO_NixOS

testRepl () {
    local nixArgs
    nixArgs=("$@")
    rm -rf repl-result-out || true # cleanup from other runs backed by a foreign nix store
    local replOutput
    replOutput="$(nix repl "${nixArgs[@]}" <<< "$replCmds")"
    echo "$replOutput"
    local outPath
    outPath=$(echo "$replOutput" |&
        grep -o -E "$NIX_STORE_DIR/\w*-simple")
    nix path-info "${nixArgs[@]}" "$outPath"
    [ "$(realpath ./repl-result-out)" == "$outPath" ] || fail "nix repl :bl doesn't make a symlink"
    # run it again without checking the output to ensure the previously created symlink gets overwritten
    nix repl "${nixArgs[@]}" <<< "$replCmds" || fail "nix repl does not work twice with the same inputs"

    # simple.nix prints a PATH during build
    echo "$replOutput" | grepQuiet -s 'PATH=' || fail "nix repl :log doesn't output logs"
    replOutput="$(nix repl "${nixArgs[@]}" <<< "$replFailingCmds" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s 'This should fail' \
      || fail "nix repl :log doesn't output logs for a failed derivation"
    replOutput="$(nix repl --show-trace "${nixArgs[@]}" <<< "$replUndefinedVariable" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s "while evaluating the file" \
      || fail "nix repl --show-trace doesn't show the trace"

    nix repl "${nixArgs[@]}" --option pure-eval true 2>&1 <<< "builtins.currentSystem" \
      | grep "attribute 'currentSystem' missing"
    nix repl "${nixArgs[@]}" 2>&1 <<< "builtins.currentSystem" \
      | grep "$(nix-instantiate --eval -E 'builtins.currentSystem')"

    # regression test for #12163
    replOutput=$(nix repl "${nixArgs[@]}" 2>&1 <<< ":sh import $testDir/simple.nix")
    echo "$replOutput" | grepInverse "error: Cannot run 'nix-shell'"

    # `:sh` and `:u` pass full config to the child process via NIX_CONFIG.
    # Previously we had warnings about deprecated unasked settings.
    echo "$replOutput" | grepInverse "'warn-short-path-literals' is deprecated"

    expectStderr 1 nix repl "${testDir}/simple.nix" \
      | grepQuiet -s "error: path \"$testDir/simple.nix\" is not a flake"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/other-root?real=$NIX_STORE_DIR"
