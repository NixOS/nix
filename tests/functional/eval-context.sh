#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Test that evalContext is shown on the error: line for various evaluation modes.

# --expr: should show "during evaluation of expression '<expr>'"
out="$(nix eval --expr '{ x = throw "boom"; }.x' 2>&1)" && fail "should have failed"
echo "$out" | grepQuiet "error: during evaluation of expression.*{ x = throw \"boom\"; }.x"

out="$(nix eval --expr 'throw "simple error"' 2>&1)" && fail "should have failed"
echo "$out" | grepQuiet "error: during evaluation of expression.*throw"

# -f / --file: should show context with attribute name or file
cat > "$TEST_ROOT/crash.nix" << 'EOF'
{ crashingAttribute = throw "file-boom"; }
EOF
out="$(nix eval crashingAttribute -f "$TEST_ROOT/crash.nix" 2>&1)" && fail "should have failed"
echo "$out" | grepQuiet "error: during evaluation of.*crashingAttribute"

# nix-build: should show "during nix-build evaluation"
out="$(nix-build --expr 'throw "nb-boom"' 2>&1)" && fail "should have failed"
echo "$out" | grepQuiet "error: during nix-build evaluation"

# nix-instantiate: should show "during nix-instantiate evaluation"
out="$(nix-instantiate --eval --expr 'throw "ni-boom"' 2>&1)" && fail "should have failed"
echo "$out" | grepQuiet "error: during nix-instantiate evaluation"

# Verify that evalContext persists even with trace truncation
cat > "$TEST_ROOT/complex.nix" << 'COMPLEXEOF'
let
  helper1 = x: { value = x; };
  helper2 = x: (helper1 x).value + 1;
  helper3 = x: helper2 (helper2 x);
  helper4 = x: helper3 (helper3 x);
  helper5 = x: helper4 (helper4 x);
  final = helper5 (throw "deep error");
in
final
COMPLEXEOF
out="$(nix eval -f "$TEST_ROOT/complex.nix" 2>&1)" && fail "should have failed"
echo "$out" | grepQuiet "error: during evaluation of file.*complex.nix"
# Should have truncation marker since there are many frames
echo "$out" | grepQuiet "stack trace truncated"
