#!/usr/bin/env bash

source common.sh

# regression #9932
echo ":env" | expect 1 nix eval --debugger --expr '(_: throw "oh snap") 42'
echo ":env" | expect 1 nix eval --debugger --expr '
  let x.a = 1; in
  with x;
  (_: builtins.seq x.a (throw "oh snap")) x.a
' >"$TEST_ROOT/debugger-test-out"
grep -P 'with: .*a' "$TEST_ROOT/debugger-test-out"
grep -P 'static: .*x' "$TEST_ROOT/debugger-test-out"
