# `outPath` recursion reaches an inner attrset whose `__toString`
# returns a non-string. Coercion of the `__toString` result then
# fails, even though the outer `outPath` traversal could otherwise succeed.
builtins.toJSON {
  outPath = {
    __toString = self: 42;
  };
}
