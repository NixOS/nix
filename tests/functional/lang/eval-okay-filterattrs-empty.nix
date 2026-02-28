# Test that filterAttrs doesn't call the function when the attrset is empty
builtins.filterAttrs (abort "function should not be called") { }
