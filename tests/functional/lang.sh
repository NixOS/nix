#!/usr/bin/env bash

source common.sh

set -o pipefail

export NIX_REMOTE=dummy://
export NIX_STORE_DIR=/nix/store

nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>&1 | grepQuiet Hello
nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>/dev/null | grepQuiet 123
nix-instantiate --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1
nix-instantiate --trace-verbose --eval -E 'builtins.traceVerbose "Hello" 123' 2>&1 | grepQuiet Hello
nix-instantiate --eval -E 'builtins.traceVerbose "Hello" 123' 2>&1 | grepQuietInverse Hello
nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1 | grepQuietInverse Hello
expectStderr 1 nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" (throw "Foo")' | grepQuiet Hello
expectStderr 1 nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello %" (throw "Foo")' | grepQuiet 'Hello %'
# Relies on parsing the expression derivation as a derivation, can't use --eval
expectStderr 1 nix-instantiate --show-trace lang/non-eval-fail-bad-drvPath.nix | grepQuiet "store path '2chwzswhhmpxbgc981i2vcz7xj4d1in9-cachix-1.7.3-bin' is not a valid derivation path"


nix-instantiate --eval -E 'let x = builtins.trace { x = x; } true; in x' \
  2>&1 | grepQuiet -E 'trace: { x = «potential infinite recursion»; }'

nix-instantiate --eval -E 'let x = { repeating = x; tracing = builtins.trace x true; }; in x.tracing'\
  2>&1 | grepQuiet -F 'trace: { repeating = «repeated»; tracing = «potential infinite recursion»; }'

nix-instantiate --eval -E 'builtins.warn "Hello" 123' 2>&1 | grepQuiet 'warning: Hello'
# shellcheck disable=SC2016 # The ${} in this is Nix, not shell
nix-instantiate --eval -E 'builtins.addErrorContext "while doing ${"something"} interesting" (builtins.warn "Hello" 123)' 2>/dev/null | grepQuiet 123

# warn does not accept non-strings for now
expectStderr 1 nix-instantiate --eval -E 'let x = builtins.warn { x = x; } true; in x' \
  | grepQuiet "expected a string but found a set"
expectStderr 1 nix-instantiate --eval --abort-on-warn -E 'builtins.warn "Hello" 123' | grepQuiet Hello
# shellcheck disable=SC2016 # The ${} in this is Nix, not shell
NIX_ABORT_ON_WARN=1 expectStderr 1 nix-instantiate --eval -E 'builtins.addErrorContext "while doing ${"something"} interesting" (builtins.warn "Hello" 123)' | grepQuiet "while doing something interesting"
