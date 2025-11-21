# Test that deepSeq reports list index and attribute name in error traces.

builtins.deepSeq [
  1
  {
    a = 2;
    b = throw "error in attr in list element";
  }
  3
] "unexpected success"
