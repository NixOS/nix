# Test that filterAttrs doesn't force values when the predicate only uses names
builtins.attrNames (
  builtins.filterAttrs (name: value: name == "a") {
    a = abort "a";
    b = abort "b";
    c = abort "c";
  }
)
