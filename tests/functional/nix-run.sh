#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStoreIfPossible

# `meta.mainProgram` selects the executable.
output="$(nix-run ./nix-run.nix -A withMainProgram)"
echo "$output" | grep -qx 'mainProgram'

# Fallback to `pname` when `meta.mainProgram` is absent.
output="$(nix-run ./nix-run.nix -A withPname)"
echo "$output" | grep -qx 'pname'

# Fallback to the parsed derivation name when neither `meta.mainProgram`
# nor `pname` is present.
output="$(nix-run ./nix-run.nix -A withParsedName)"
echo "$output" | grep -qx 'parsedName'

# Default attribute (top-level) works without `-A`, using the root
# value returned by the expression.
output="$(nix-run -E '(import ./nix-run.nix).topLevel')"
echo "$output" | grep -qx 'mainProgram'

# Arguments after `--` are forwarded verbatim to the program.
output="$(nix-run ./nix-run.nix -A echoArgs -- hello 'a b' --flag)"
echo "$output" | grep -qx 'echoArgs'
echo "$output" | grep -qx 'arg:hello'
echo "$output" | grep -qx 'arg:a b'
echo "$output" | grep -qx 'arg:--flag'

# Default FILE is `./default.nix`: run from a temporary directory that
# has its own `default.nix` re-exporting the test fixture.
runDir="$TEST_ROOT/nix-run-default"
mkdir -p "$runDir"
cat > "$runDir/default.nix" <<EOF
import $PWD/nix-run.nix
EOF
(
    cd "$runDir"
    output="$(nix-run -A withPname)"
    echo "$output" | grep -qx 'pname'
)

# Missing attribute produces a clear error.
expectStderr 1 nix-run ./nix-run.nix -A doesNotExist \
    | grep -q 'doesNotExist'
