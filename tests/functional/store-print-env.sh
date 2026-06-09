#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Regression test for nix-store --print-env argument escaping
# This tests that arguments in _args are properly escaped as a single string
# rather than double-escaped which could lead to command injection

cat > "$TEST_ROOT/test-args.nix" <<'EOF'
derivation {
  name = "test-print-env-args";
  system = builtins.currentSystem;
  builder = "/bin/sh";
  args = [ "-c" "echo hello world" ];
}
EOF

drvPath=$(nix-instantiate "$TEST_ROOT/test-args.nix")
output=$(nix-store --print-env "$drvPath" | grep "^export _args")

# The output should be: export _args; _args='-c echo hello world'
# NOT: export _args; _args=''-c' 'echo hello world''

# Test that it can be safely evaluated
eval "$output"
expected="-c echo hello world"
# shellcheck disable=SC2154 # _args is set by the eval above
if [ "$_args" != "$expected" ]; then
    echo "ERROR: _args not properly escaped!"
    echo "Expected: $expected"
    echo "Got: $_args"
    echo "Raw output: $output"
    exit 1
fi
