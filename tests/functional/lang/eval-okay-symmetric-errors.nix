# Test symmetric error handling for binary operators.
# The order of operands should not affect the error behavior.

let
  # Helper to test an operator with throw combinations (non-aborting)
  testOp = op: {
    throwThrow = builtins.tryEval (op (throw "x") (throw "y"));
    throwNormal = builtins.tryEval (op (throw "x") 1);
    normalThrow = builtins.tryEval (op 1 (throw "y"));
  };
in
{
  # Arithmetic operators
  add = testOp builtins.add;
  sub = testOp builtins.sub;
  mul = testOp builtins.mul;
  div = testOp builtins.div;

  # Bitwise operators
  bitAnd = testOp builtins.bitAnd;
  bitOr = testOp builtins.bitOr;
  bitXor = testOp builtins.bitXor;

  # Comparison operator
  lessThan = testOp builtins.lessThan;

  # Equality operators (using inline lambdas since == and != aren't functions)
  eq = testOp (a: b: a == b);
  neq = testOp (a: b: a != b);

  # List concatenation (using inline lambda since ++ isn't a function)
  concatLists = testOp (a: b: a ++ b);

  # String/path concatenation (using inline lambda since + isn't a function for this)
  concatStrings = testOp (a: b: a + b);
}
