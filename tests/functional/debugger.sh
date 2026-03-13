#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# regression #9932
echo ":env" | expect 1 nix eval --debugger --expr '(_: throw "oh snap") 42'
echo ":env" | expect 1 nix eval --debugger --expr '
  let x.a = 1; in
  with x;
  (_: builtins.seq x.a (throw "oh snap")) x.a
' >debugger-test-out
grep -P 'with: .*a' debugger-test-out
grep -P 'static: .*x' debugger-test-out

# Test that debugger triggers on fetchTree errors and can access let bindings
out=$(echo -e "args\n:quit" | expect 1 nix eval --debugger --impure --expr '
  let
    args = { type = "git"; url = "nonexistent-repo"; };
  in
  builtins.fetchTree args
' 2>/dev/null)
[[ "$out" == *'"git"'* ]] || fail "debugger should print args.type"
[[ "$out" == *'"nonexistent-repo"'* ]] || fail "debugger should print args.url"
